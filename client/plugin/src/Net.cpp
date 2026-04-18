#include "Net.h"

#include <array>
#include <chrono>
#include <vector>

#include <flatbuffers/flatbuffers.h>

#include "lifecycle_generated.h"
#include "types_generated.h"
#include "world_generated.h"

#include <RE/Skyrim.h>
#include <SKSE/SKSE.h>
#include <SKSE/Logger.h>

#include "Combat.h"
#include "Ghost.h"
#include "Plugin.h"
#include "Socket.h"
#include "Zeus.h"
#include "ZeusOverlay.h"

namespace re_v1 = skyrim_relive::v1;

namespace {
    constexpr std::uint8_t kProtoVersion = 2;
    constexpr std::size_t kHeaderLen = 4;

    void encode_packet(re_v1::MessageType type, std::span<const std::uint8_t> body,
                       std::vector<std::uint8_t>& out) {
        out.clear();
        out.reserve(kHeaderLen + body.size());
        out.push_back(static_cast<std::uint8_t>('R'));
        out.push_back(static_cast<std::uint8_t>('L'));
        out.push_back(kProtoVersion);
        out.push_back(static_cast<std::uint8_t>(type));
        out.insert(out.end(), body.begin(), body.end());
    }

    bool parse_packet(std::span<const std::uint8_t> packet, re_v1::MessageType& type,
                      std::span<const std::uint8_t>& body) {
        if (packet.size() < kHeaderLen) return false;
        if (packet[0] != 'R' || packet[1] != 'L') return false;
        if (packet[2] != kProtoVersion) return false;
        type = static_cast<re_v1::MessageType>(packet[3]);
        body = packet.subspan(kHeaderLen);
        return true;
    }
    void sync_zeus_to_overlay() {
        auto npcs = relive::zeus::list_npcs();
        std::vector<relive::zeus_overlay::OverlayNpc> on;
        for (const auto& n : npcs) {
            on.push_back({n.zeus_id, n.base_form_id});
        }
        relive::zeus_overlay::push_npc_list(on.data(),
            static_cast<unsigned>(on.size()));
    }
}

namespace relive::net {

    Client::~Client() {
        stop();
    }

    bool Client::start(std::string_view host, std::uint16_t port,
                       std::string_view player_name,
                       const CharacterData& char_data) {
        const auto h = sock::udp_connect_ipv4(host, port);
        if (h == sock::kInvalid) {
            SKSE::log::error("UDP connect failed");
            return false;
        }
        socket_ = h;

        if (!send_hello(player_name, char_data) || !wait_for_welcome()) {
            sock::close(socket_);
            socket_ = sock::kInvalid;
            return false;
        }

        running_.store(true, std::memory_order_release);
        thread_ = std::thread([this]() { run(); });
        SKSE::log::info("net client started; sending PlayerInput @60Hz");
        return true;
    }

    void Client::stop() {
        if (!running_.exchange(false, std::memory_order_acq_rel)) return;
        if (thread_.joinable()) {
            // Detached so process exit isn't blocked on the recv timeout.
            thread_.detach();
        }
        sock::close(socket_);
        socket_ = sock::kInvalid;
    }

    bool Client::send_hello(std::string_view name, const CharacterData& cd) {
        flatbuffers::FlatBufferBuilder fbb(256);
        const std::string name_s{name};
        const auto name_off = fbb.CreateString(name_s);
        const auto char_name_off = fbb.CreateString(cd.character_name);

        std::vector<flatbuffers::Offset<re_v1::SkillEntry>> skill_offsets;
        for (const auto& sk : cd.top_skills) {
            const auto sn = fbb.CreateString(sk.name);
            re_v1::SkillEntryBuilder sb(fbb);
            sb.add_name(sn);
            sb.add_level(sk.value);
            skill_offsets.push_back(sb.Finish());
        }
        const auto skills_vec = fbb.CreateVector(skill_offsets);

        re_v1::HelloBuilder hb(fbb);
        hb.add_name(name_off);
        hb.add_client_protocol_version(kProtoVersion);
        hb.add_character_name(char_name_off);
        hb.add_character_level(cd.level);
        hb.add_top_skills(skills_vec);
        const auto hello_off = hb.Finish();
        fbb.Finish(hello_off);

        std::vector<std::uint8_t> packet;
        encode_packet(re_v1::MessageType_Hello,
                      std::span(fbb.GetBufferPointer(), fbb.GetSize()), packet);

        if (!sock::send_all(socket_, packet)) {
            SKSE::log::error("Hello send failed");
            return false;
        }
        SKSE::log::info("Hello sent: char='{}' level={} skills={}",
                        cd.character_name, cd.level, cd.top_skills.size());
        return true;
    }

    bool Client::wait_for_welcome() {
        sock::set_recv_timeout(socket_, 2000);

        std::array<std::uint8_t, 2048> buf{};
        const int n = sock::recv_one(socket_, buf);
        if (n <= 0) {
            SKSE::log::error("timed out waiting for Welcome");
            return false;
        }

        re_v1::MessageType type;
        std::span<const std::uint8_t> body;
        if (!parse_packet(std::span(buf.data(), static_cast<std::size_t>(n)), type, body)) {
            SKSE::log::error("malformed reply to Hello");
            return false;
        }
        if (type == re_v1::MessageType_Disconnect) {
            const auto* d = flatbuffers::GetRoot<re_v1::Disconnect>(body.data());
            SKSE::log::error("server Disconnect: code={} reason={}",
                             static_cast<int>(d->code()),
                             d->reason() ? d->reason()->c_str() : "");
            return false;
        }
        if (type != re_v1::MessageType_Welcome) {
            SKSE::log::error("expected Welcome, got type {}", static_cast<int>(type));
            return false;
        }

        const auto* w = flatbuffers::GetRoot<re_v1::Welcome>(body.data());
        player_id_ = w->player_id();
        SKSE::log::info("Welcome: player_id={} tick={}Hz snap={}Hz", player_id_,
                        static_cast<int>(w->server_tick_rate_hz()),
                        static_cast<int>(w->server_snapshot_rate_hz()));
        return true;
    }

    void Client::run() {
        // Short recv timeout so we don't blow the next 16ms tick deadline.
        sock::set_recv_timeout(socket_, 5);

        auto next = std::chrono::steady_clock::now();
        const auto period = std::chrono::microseconds(16667);

        while (running_.load(std::memory_order_acquire)) {
            send_player_input();
            drain_incoming();
            next += period;
            std::this_thread::sleep_until(next);
        }
    }

    void Client::send_player_input() {
        const auto* player = RE::PlayerCharacter::GetSingleton();
        // parentCell is non-null only when the player is in a loaded world.
        // Skip on main-menu / save-load-in-progress — reading a half-torn
        // PlayerCharacter from this background thread is undefined and has
        // crashed in the wild.
        if (!player || !player->parentCell) {
            return;
        }
        const auto pos = player->GetPosition();
        const float x = pos.x;
        const float y = pos.y;
        const float z = pos.z;
        const float yaw = player->GetAngleZ();
        last_x_.store(x, std::memory_order_relaxed);
        last_y_.store(y, std::memory_order_relaxed);
        last_z_.store(z, std::memory_order_relaxed);
        last_yaw_.store(yaw, std::memory_order_relaxed);

        // Phase 2.1: read locomotion variables from the player's behavior
        // graph. SetGraphVariable... returns false silently if the variable
        // doesn't exist — defaults stay zero / false in that case.
        float anim_speed = 0.0F;
        float anim_dir = 0.0F;
        bool anim_running = false;
        bool anim_sprinting = false;
        bool anim_sneaking = false;
        bool anim_equipping = false;
        bool anim_unequipping = false;
        std::int32_t anim_weapon_state = 0;
        player->GetGraphVariableFloat("Speed", anim_speed);
        player->GetGraphVariableFloat("Direction", anim_dir);
        player->GetGraphVariableBool("IsRunning", anim_running);
        player->GetGraphVariableBool("IsSprinting", anim_sprinting);
        player->GetGraphVariableBool("IsSneaking", anim_sneaking);
        player->GetGraphVariableBool("IsEquipping", anim_equipping);
        player->GetGraphVariableBool("IsUnequipping", anim_unequipping);
        player->GetGraphVariableInt("iState", anim_weapon_state);
        const bool weapon_drawn = player->AsActorState()->IsWeaponDrawn();
        const float pitch = player->GetAngleX();
        // Only send real cell FormID for interiors — exterior cells have
        // nonzero FormIDs too but players in adjacent grid squares would
        // mismatch and thrash ghost spawn/despawn. Exterior = 0 (wildcard)
        // lets the distance gate handle cross-terrain culling instead.
        const std::uint32_t cell_form_id =
            (player->parentCell && player->parentCell->IsInteriorCell())
                ? player->parentCell->GetFormID() : 0;

        flatbuffers::FlatBufferBuilder fbb(128);
        const re_v1::Vec3 v3(x, y, z);
        const re_v1::Transform tr(v3, yaw);
        const auto now_ms = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch())
                .count());
        re_v1::PlayerInputBuilder ib(fbb);
        ib.add_transform(&tr);
        ib.add_client_time_ms(now_ms);
        ib.add_anim_speed(anim_speed);
        ib.add_anim_direction(anim_dir);
        ib.add_anim_is_running(anim_running);
        ib.add_anim_is_sprinting(anim_sprinting);
        ib.add_anim_is_sneaking(anim_sneaking);
        ib.add_anim_is_equipping(anim_equipping);
        ib.add_anim_is_unequipping(anim_unequipping);
        ib.add_anim_weapon_state(anim_weapon_state);
        ib.add_weapon_drawn(weapon_drawn);
        ib.add_pitch(pitch);
        ib.add_cell_form_id(cell_form_id);
        const auto input_off = ib.Finish();
        fbb.Finish(input_off);

        std::vector<std::uint8_t> packet;
        encode_packet(re_v1::MessageType_PlayerInput,
                      std::span(fbb.GetBufferPointer(), fbb.GetSize()), packet);

        if (sock::send_all(socket_, packet)) {
            player_inputs_sent_.fetch_add(1, std::memory_order_relaxed);
        }
    }

    void Client::drain_incoming() {
        std::array<std::uint8_t, 2048> buf{};
        for (;;) {
            const int n = sock::recv_one(socket_, buf);
            if (n <= 0) return;

            re_v1::MessageType type;
            std::span<const std::uint8_t> body;
            if (!parse_packet(std::span(buf.data(), static_cast<std::size_t>(n)), type, body)) {
                continue;
            }

            if (type == re_v1::MessageType_WorldSnapshot) {
                snapshots_received_.fetch_add(1, std::memory_order_relaxed);
                const auto* snap = flatbuffers::GetRoot<re_v1::WorldSnapshot>(body.data());
                std::vector<ghost::Manager::PlayerUpdate> updates;
                if (const auto* players = snap->players()) {
                    updates.reserve(players->size());
                    for (const auto* p : *players) {
                        ghost::Manager::PlayerUpdate u;
                        u.player_id = p->player_id();
                        // PlayerState is a v2 table — transform() returns
                        // a pointer that may be null on malformed packets.
                        const auto* t = p->transform();
                        if (!t) continue;
                        const auto& pos = t->pos();
                        u.snap.server_tick = snap->server_tick();
                        u.snap.server_time_ms = snap->server_time_ms();
                        u.snap.x = pos.x();
                        u.snap.y = pos.y();
                        u.snap.z = pos.z();
                        u.snap.yaw = t->yaw();
                        // Phase 2.1: locomotion anim variables.
                        u.snap.speed = p->anim_speed();
                        u.snap.direction = p->anim_direction();
                        u.snap.is_running = p->anim_is_running();
                        u.snap.is_sprinting = p->anim_is_sprinting();
                        u.snap.is_sneaking = p->anim_is_sneaking();
                        // Phase 2.2: weapon state.
                        u.snap.is_equipping = p->anim_is_equipping();
                        u.snap.is_unequipping = p->anim_is_unequipping();
                        u.snap.weapon_state = p->anim_weapon_state();
                        u.snap.weapon_drawn = p->weapon_drawn();
                        u.snap.pitch = p->pitch();
                        u.snap.cell_form_id = p->cell_form_id();
                        updates.push_back(u);
                    }
                }
                ghost::instance().ingest(snap->server_tick(),
                                         snap->server_time_ms(), updates);
            } else if (type == re_v1::MessageType_PlayerList) {
                const auto* pl = flatbuffers::GetRoot<re_v1::PlayerList>(body.data());
                std::vector<relive::plugin::PlayerEntry> entries;
                if (const auto* players = pl->players()) {
                    entries.reserve(players->size());
                    for (const auto* e : *players) {
                        relive::plugin::PlayerEntry pe;
                        pe.player_id = e->player_id();
                        pe.display_name = e->display_name() ? e->display_name()->str() : "";
                        pe.character_name = e->character_name() ? e->character_name()->str() : "";
                        pe.level = e->character_level();
                        if (const auto* skills = e->top_skills()) {
                            for (const auto* s : *skills) {
                                pe.top_skills.emplace_back(
                                    s->name() ? s->name()->str() : "?", s->level());
                            }
                        }
                        if (const auto* p = e->pos()) {
                            pe.x = p->x();
                            pe.y = p->y();
                            pe.z = p->z();
                        }
                        pe.cell_form_id = e->cell_form_id();
                        pe.hp = e->hp();
                        pe.hp_max = e->hp_max();
                        entries.push_back(std::move(pe));
                    }
                }
                plugin::update_player_list(
                    static_cast<decltype(entries)&&>(entries));
                // Push to Zeus overlay.
                std::vector<zeus_overlay::OverlayPlayer> op;
                for (const auto& e : plugin::get_player_list()) {
                    zeus_overlay::OverlayPlayer p{};
                    p.player_id = e.player_id;
                    strncpy(p.name, e.display_name.c_str(), sizeof(p.name) - 1);
                    strncpy(p.character_name, e.character_name.c_str(),
                            sizeof(p.character_name) - 1);
                    p.level = e.level;
                    p.hp = e.hp;
                    p.hp_max = e.hp_max;
                    op.push_back(p);
                }
                zeus_overlay::push_player_list(op.data(),
                    static_cast<unsigned>(op.size()));
            } else if (type == re_v1::MessageType_ChatMessage) {
                const auto* cm = flatbuffers::GetRoot<re_v1::ChatMessage>(body.data());
                const auto sender = cm->sender_name() ? cm->sender_name()->str() : "???";
                const auto text = cm->text() ? cm->text()->str() : "";
                if (!text.empty()) {
                    if (auto* task = SKSE::GetTaskInterface()) {
                        const auto pid = cm->player_id();
                        task->AddTask([sender, text, pid]() {
                            if (auto* console = RE::ConsoleLog::GetSingleton()) {
                                console->Print("[Chat] %s: %s", sender.c_str(), text.c_str());
                            }
                        });
                    }
                    SKSE::log::info("[chat] {}: {}", sender, text);
                }
            } else if (type == re_v1::MessageType_AdminAuthResult) {
                const auto* r = flatbuffers::GetRoot<re_v1::AdminAuthResult>(body.data());
                const bool ok = r->success();
                const auto reason = r->reason() ? r->reason()->str() : "";
                if (auto* task = SKSE::GetTaskInterface()) {
                    task->AddTask([ok, reason]() {
                        if (auto* console = RE::ConsoleLog::GetSingleton()) {
                            console->Print("[SkyrimReLive] admin: %s",
                                           reason.c_str());
                        }
                    });
                }
                SKSE::log::info("AdminAuthResult: success={} reason={}", ok, reason);
            } else if (type == re_v1::MessageType_AdminCommandResult) {
                const auto* r = flatbuffers::GetRoot<re_v1::AdminCommandResult>(body.data());
                const bool ok = r->success();
                const auto msg = r->message() ? r->message()->str() : "";
                if (auto* task = SKSE::GetTaskInterface()) {
                    task->AddTask([ok, msg]() {
                        if (auto* console = RE::ConsoleLog::GetSingleton()) {
                            console->Print("[Admin] %s%s",
                                           ok ? "" : "ERROR: ", msg.c_str());
                        }
                    });
                }
            } else if (type == re_v1::MessageType_ServerCommand) {
                const auto* sc = flatbuffers::GetRoot<re_v1::ServerCommand>(body.data());
                const auto cmd = sc->command() ? sc->command()->str() : "";
                const auto args = sc->args() ? sc->args()->str() : "";
                if (auto* task = SKSE::GetTaskInterface()) {
                    task->AddTask([cmd, args]() {
                        if (cmd == "time") {
                            float hour = 12.0F;
                            try { hour = std::stof(args); } catch (...) {}
                            auto* cal = RE::Calendar::GetSingleton();
                            if (cal && cal->gameHour) {
                                cal->gameHour->value = hour;
                            }
                            if (auto* c = RE::ConsoleLog::GetSingleton()) {
                                c->Print("[Server] Time set to %.0f:00", hour);
                            }
                        } else if (cmd == "tp") {
                            float tx = 0, ty = 0, tz = 0;
                            std::istringstream iss(args);
                            iss >> tx >> ty >> tz;
                            auto* player = RE::PlayerCharacter::GetSingleton();
                            if (player) {
                                player->SetPosition({tx, ty, tz}, true);
                                if (auto* c = RE::ConsoleLog::GetSingleton()) {
                                    c->Print("[Server] Teleported to (%.0f, %.0f, %.0f)",
                                             tx, ty, tz);
                                }
                            }
                        } else if (cmd == "give") {
                            std::uint32_t formId = 0;
                            std::uint32_t count = 1;
                            std::istringstream iss(args);
                            std::string formStr;
                            iss >> formStr >> count;
                            try { formId = std::stoul(formStr, nullptr, 0); } catch (...) {}
                            if (formId != 0) {
                                zeus::execute_give(formId, count);
                                if (auto* c = RE::ConsoleLog::GetSingleton()) {
                                    c->Print("[Server] Received %u item(s)", count);
                                }
                            }
                        } else if (cmd == "spawn") {
                            std::uint32_t formId = 0;
                            float sx = 0, sy = 0, sz = 0;
                            std::istringstream iss(args);
                            std::string formStr;
                            iss >> formStr >> sx >> sy >> sz;
                            try { formId = std::stoul(formStr, nullptr, 0); } catch (...) {}
                            if (formId != 0) {
                                auto actor = zeus::execute_spawn(formId, sx, sy, sz);
                                if (auto* c = RE::ConsoleLog::GetSingleton()) {
                                    if (actor) {
                                        auto npcs = zeus::list_npcs();
                                        auto zid = npcs.empty() ? 0 : npcs.back().zeus_id;
                                        c->Print("[Server] NPC spawned (zeus_id=%u)", zid);
                                    } else {
                                        auto objs = zeus::list_objects();
                                        auto zid = objs.empty() ? 0 : objs.back().zeus_id;
                                        c->Print("[Server] Object placed (zeus_id=%u)", zid);
                                    }
                                }
                                sync_zeus_to_overlay();
                            }
                        } else if (cmd == "obj") {
                            std::uint32_t zid = 0;
                            std::string order;
                            std::string order_args;
                            std::istringstream iss(args);
                            iss >> zid >> order;
                            std::getline(iss, order_args);
                            while (!order_args.empty() &&
                                   std::isspace(static_cast<unsigned char>(order_args.front())))
                                order_args.erase(order_args.begin());
                            auto result = zeus::execute_obj_order(zid, order, order_args);
                            if (auto* c = RE::ConsoleLog::GetSingleton()) {
                                c->Print("[Zeus Obj %u] %s", zid, result.c_str());
                            }
                            sync_zeus_to_overlay();
                        } else if (cmd == "npcs") {
                            auto npcs = zeus::list_npcs();
                            if (auto* c = RE::ConsoleLog::GetSingleton()) {
                                if (npcs.empty()) {
                                    c->Print("[Zeus] No spawned NPCs");
                                } else {
                                    c->Print("[Zeus] %u spawned NPC(s):", static_cast<unsigned>(npcs.size()));
                                    for (const auto& n : npcs) {
                                        c->Print("  zeus_id=%u base=0x%x", n.zeus_id, n.base_form_id);
                                    }
                                }
                            }
                        } else if (cmd == "npc") {
                            // args: "<zeus_id> <order> [order_args...]"
                            std::uint32_t zid = 0;
                            std::string order;
                            std::string order_args;
                            std::istringstream iss(args);
                            iss >> zid >> order;
                            std::getline(iss, order_args);
                            while (!order_args.empty() &&
                                   std::isspace(static_cast<unsigned char>(order_args.front())))
                                order_args.erase(order_args.begin());
                            auto result = zeus::execute_npc_order(zid, order, order_args);
                            if (auto* c = RE::ConsoleLog::GetSingleton()) {
                                c->Print("[Zeus NPC %u] %s", zid, result.c_str());
                            }
                            sync_zeus_to_overlay();
                        } else if (cmd == "weather") {
                            RE::FormID formId = 0;
                            try { formId = std::stoul(args, nullptr, 0); } catch (...) {}
                            if (formId != 0) {
                                auto* weather = RE::TESForm::LookupByID<RE::TESWeather>(formId);
                                auto* sky = RE::Sky::GetSingleton();
                                if (weather && sky) {
                                    sky->ForceWeather(weather, true);
                                }
                            }
                            if (auto* c = RE::ConsoleLog::GetSingleton()) {
                                c->Print("[Server] Weather changed");
                            }
                        }
                        SKSE::log::info("ServerCommand: cmd={} args={}", cmd, args);
                    });
                }
            } else if (type == re_v1::MessageType_DamageApply) {
                const auto* d = flatbuffers::GetRoot<re_v1::DamageApply>(body.data());
                combat::on_damage_apply(d->attacker_player_id(), d->damage(),
                                        d->stagger(), d->new_hp());
            } else if (type == re_v1::MessageType_Disconnect) {
                const auto* d = flatbuffers::GetRoot<re_v1::Disconnect>(body.data());
                SKSE::log::warn("server sent Disconnect: code={} reason={}",
                                static_cast<int>(d->code()),
                                d->reason() ? d->reason()->c_str() : "");
                running_.store(false, std::memory_order_release);
                return;
            }
        }
    }

    void Client::send_combat_event(std::uint32_t target_player_id,
                                   std::uint8_t attack_type, float weapon_reach,
                                   float weapon_base_damage,
                                   std::uint8_t attack_class) {
        if (!running_.load(std::memory_order_acquire)) return;

        const auto now_ms = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch())
                .count());

        flatbuffers::FlatBufferBuilder fbb(64);
        re_v1::CombatEventBuilder cb(fbb);
        cb.add_target_player_id(target_player_id);
        cb.add_attack_type(attack_type);
        cb.add_weapon_reach(weapon_reach);
        cb.add_weapon_base_damage(weapon_base_damage);
        cb.add_client_time_ms(now_ms);
        cb.add_attack_class(
            static_cast<re_v1::AttackClass>(attack_class));
        const auto off = cb.Finish();
        fbb.Finish(off);

        std::vector<std::uint8_t> packet;
        encode_packet(re_v1::MessageType_CombatEvent,
                      std::span(fbb.GetBufferPointer(), fbb.GetSize()), packet);
        sock::send_all(socket_, packet);
    }

    void Client::send_admin_auth(std::string_view password) {
        if (!running_.load(std::memory_order_acquire)) return;
        flatbuffers::FlatBufferBuilder fbb(64);
        const std::string pw{password};
        const auto pw_off = fbb.CreateString(pw);
        re_v1::AdminAuthBuilder ab(fbb);
        ab.add_password(pw_off);
        fbb.Finish(ab.Finish());
        std::vector<std::uint8_t> packet;
        encode_packet(re_v1::MessageType_AdminAuth,
                      std::span(fbb.GetBufferPointer(), fbb.GetSize()), packet);
        sock::send_all(socket_, packet);
    }

    void Client::send_admin_command(std::string_view command) {
        if (!running_.load(std::memory_order_acquire)) return;
        flatbuffers::FlatBufferBuilder fbb(128);
        const std::string cmd{command};
        const auto cmd_off = fbb.CreateString(cmd);
        re_v1::AdminCommandBuilder cb(fbb);
        cb.add_command(cmd_off);
        fbb.Finish(cb.Finish());
        std::vector<std::uint8_t> packet;
        encode_packet(re_v1::MessageType_AdminCommand,
                      std::span(fbb.GetBufferPointer(), fbb.GetSize()), packet);
        sock::send_all(socket_, packet);
    }

    void Client::send_chat(std::string_view text) {
        if (!running_.load(std::memory_order_acquire)) return;
        flatbuffers::FlatBufferBuilder fbb(128);
        const std::string text_s{text};
        const auto text_off = fbb.CreateString(text_s);
        re_v1::ChatMessageBuilder cb(fbb);
        cb.add_text(text_off);
        const auto off = cb.Finish();
        fbb.Finish(off);
        std::vector<std::uint8_t> packet;
        encode_packet(re_v1::MessageType_ChatMessage,
                      std::span(fbb.GetBufferPointer(), fbb.GetSize()), packet);
        sock::send_all(socket_, packet);
    }

}
