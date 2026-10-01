/*
    debug_bridge.asi — GTA SA MCP Debug Bridge
    
    Activates only when mcp_debug.lock exists in the game directory.
    Opens a named pipe server, processes MCP commands each frame.
    
    Protocol: newline-delimited JSON over \\.\pipe\gtasa_mcp_bridge
      Request:  {"id":N,"cmd":"...","field":...}
      Response: {"id":N,"ok":true,"data":{...}}
               {"id":N,"ok":false,"error":"..."}

    Build: add to Plugin_SA.vcxproj or build standalone as a separate .asi
    Requires: plugin-sdk headers (same include paths as any plugin)
*/
#include <plugin.h>
#include <GameVersion.h>
#include <CWorld.h>
#include <CTimer.h>
#include <CClock.h>
#include <CPools.h>
#include <CPlayerPed.h>
#include <CVehicle.h>
#include <CAutomobile.h>
#include <CBike.h>
#include <CBmx.h>
#include <CBoat.h>
#include <CHeli.h>
#include <CMonsterTruck.h>
#include <CPlane.h>
#include <CQuadBike.h>
#include <CTrailer.h>
#include <CTrain.h>
#include <CCivilianPed.h>
#include <CStreaming.h>
#include <CModelInfo.h>
#include <CTheScripts.h>
#include <CWanted.h>
#include <CTaskComplexWanderStandard.h>
#include <CTaskComplexKillPedOnFoot.h>
#include <CWeather.h>
#include <CTaskSimpleStandStill.h>
#include <CTaskComplexDie.h>
#include <CMenuManager.h>
#include <extensions/PoolIterator.h>
#include <enums/eWeaponType.h>
#include <enums/eTaskType.h>
#include <enums/eAnimations.h>

#include <windows.h>
#include <string>
#include <deque>
#include <mutex>
#include <thread>
#include <sstream>
#include <iomanip>

// ---- simple JSON builder ------------------------------------------------

static std::string jsonStr(const std::string& s) {
    std::string r = "\"";
    for (char c : s) {
        if (c == '"') r += "\\\"";
        else if (c == '\\') r += "\\\\";
        else if (c == '\n') r += "\\n";
        else r += c;
    }
    return r + "\"";
}

static std::string fmtf(float v) {
    char buf[32]; sprintf_s(buf, "%.4f", v); return buf;
}

// ---- minimal JSON parser (only what we need) ----------------------------
// Parses flat {"key":value} — no nesting, no arrays. Sufficient for our protocol.

struct JVal { std::string s; double n = 0; bool isNum = false; bool isNull = true; };

static std::string jsonGetStr(const std::string& json, const std::string& key, const std::string& def = "") {
    std::string search = "\"" + key + "\"";
    auto pos = json.find(search);
    if (pos == std::string::npos) return def;
    pos = json.find(':', pos); if (pos == std::string::npos) return def;
    pos = json.find_first_not_of(" \t:", pos);
    if (pos == std::string::npos) return def;
    if (json[pos] == '"') {
        auto end = json.find('"', pos + 1);
        if (end == std::string::npos) return def;
        return json.substr(pos + 1, end - pos - 1);
    }
    return def;
}

static double jsonGetNum(const std::string& json, const std::string& key, double def = 0.0) {
    std::string search = "\"" + key + "\"";
    auto pos = json.find(search);
    if (pos == std::string::npos) return def;
    pos = json.find(':', pos); if (pos == std::string::npos) return def;
    pos = json.find_first_not_of(" \t:", pos);
    if (pos == std::string::npos) return def;
    char* end; double v = strtod(json.c_str() + pos, &end);
    return (end != json.c_str() + pos) ? v : def;
}

static bool jsonHasKey(const std::string& json, const std::string& key) {
    return json.find("\"" + key + "\"") != std::string::npos;
}

static bool jsonGetBool(const std::string& json, const std::string& key, bool def = false) {
    std::string search = "\"" + key + "\"";
    auto pos = json.find(search);
    if (pos == std::string::npos) return def;
    pos = json.find(':', pos); if (pos == std::string::npos) return def;
    pos = json.find_first_not_of(" \t:", pos);
    if (pos == std::string::npos) return def;
    if (json.compare(pos, 4, "true") == 0) return true;
    if (json.compare(pos, 5, "false") == 0) return false;
    return def;
}

// ---- command queue -------------------------------------------------------

struct BridgeCommand { int id; std::string cmd; std::string json; };
struct Response { std::string line; };

static std::deque<BridgeCommand>  g_cmdQueue;
static std::deque<Response> g_respQueue;
static std::mutex           g_mutex;

// ---- ped/vehicle handle map ---------------------------------------------
// SCM-style refs (slot index << 8 | generation) so stale handles to recycled
// pool slots resolve to nullptr instead of silently hitting the wrong entity.

static CPed*    HandleToPed(int handle) {
    if (!CPools::ms_pPedPool) return nullptr;
    return CPools::ms_pPedPool->GetAtRef(handle);
}
static CVehicle* HandleToVehicle(int handle) {
    if (!CPools::ms_pVehiclePool) return nullptr;
    return CPools::ms_pVehiclePool->GetAtRef(handle);
}
static int PedToHandle(CPed* ped) {
    return CPools::ms_pPedPool ? CPools::ms_pPedPool->GetRef(ped) : -1;
}
static int VehicleToHandle(CVehicle* veh) {
    return CPools::ms_pVehiclePool ? CPools::ms_pVehiclePool->GetRef(veh) : -1;
}

// ---- command handlers (run on game thread via gameProcessEvent) ----------

static std::string handleCommand(const std::string& cmd, const std::string& json) {
    // ---- player_get ----
    if (cmd == "player_get") {
        CPlayerPed* player = FindPlayerPed(0);
        if (!player) return "null";
        CVector pos = player->GetPosition();
        CVehicle* veh = FindPlayerVehicle(0, false);
        std::ostringstream o;
        o << "{"
          << "\"x\":" << fmtf(pos.x) << ","
          << "\"y\":" << fmtf(pos.y) << ","
          << "\"z\":" << fmtf(pos.z) << ","
          << "\"heading\":" << fmtf(player->GetHeading() * 57.2957795f) << ","
          << "\"health\":" << fmtf(player->m_fHealth) << ","
          << "\"armour\":" << fmtf(player->m_fArmour) << ","
          << "\"wanted\":" << (player->m_pPlayerData && player->m_pPlayerData->m_pWanted ? (int)player->m_pPlayerData->m_pWanted->m_nWantedLevel : 0) << ","
          << "\"money\":" << CWorld::Players[0].m_nMoney << ","
          << "\"interior\":" << (int)player->m_nAreaCode << ","
          << "\"inVehicle\":" << (player->bInVehicle ? "true" : "false") << ","
          << "\"vehicleHandle\":" << (veh ? VehicleToHandle(veh) : -1)
          << "}";
        return o.str();
    }

    // ---- player_set ----
    if (cmd == "player_set") {
        CPlayerPed* player = FindPlayerPed(0);
        if (!player) return "null";
        if (jsonHasKey(json, "health")) player->m_fHealth = (float)jsonGetNum(json, "health");
        if (jsonHasKey(json, "armour")) player->m_fArmour = (float)jsonGetNum(json, "armour");
        if (jsonHasKey(json, "money"))  CWorld::Players[0].m_nMoney = (int)jsonGetNum(json, "money");
        if (jsonHasKey(json, "wanted") && player->m_pPlayerData && player->m_pPlayerData->m_pWanted) {
            int w = (int)jsonGetNum(json, "wanted");
            player->m_pPlayerData->m_pWanted->SetWantedLevel(w);
        }
        return "{}";
    }

    // ---- player_teleport ----
    if (cmd == "player_teleport") {
        CPlayerPed* player = FindPlayerPed(0);
        if (!player) return "null";
        float x = (float)jsonGetNum(json, "x");
        float y = (float)jsonGetNum(json, "y");
        float z = (float)jsonGetNum(json, "z");
        CVector pos = { x, y, z };
        // Remove from vehicle first
        if (player->bInVehicle && player->m_pVehicle) {
            player->m_pVehicle->RemoveDriver(false);
            player->bInVehicle = false;
        }
        player->Teleport(pos, false);
        if (jsonHasKey(json, "heading")) {
            float h = (float)jsonGetNum(json, "heading") * 0.0174532925f;
            player->SetHeading(h);
        }
        return "{}";
    }

    // ---- player_give_weapon ----
    if (cmd == "player_give_weapon") {
        CPlayerPed* player = FindPlayerPed(0);
        if (!player) return "null";
        int wt = (int)jsonGetNum(json, "weaponType");
        int ammo = (int)jsonGetNum(json, "ammo");
        bool setActive = jsonGetBool(json, "setActive", true);
        // GiveWeapon handles model loading internally — just mark deletable after
        player->GiveWeapon((eWeaponType)wt, ammo, true);
        if (setActive) player->SetCurrentWeapon((eWeaponType)wt);
        return "{}";
    }

    // ---- vehicle_spawn ----
    if (cmd == "vehicle_spawn") {
        CPlayerPed* player = FindPlayerPed(0);
        if (!player) return "null";
        int modelId = (int)jsonGetNum(json, "modelId");
        float ox = (float)jsonGetNum(json, "offsetX", 5.0);
        float oy = (float)jsonGetNum(json, "offsetY", 0.0);

        unsigned char oldFlags = CStreaming::ms_aInfoForModel[modelId].m_nFlags;
        CStreaming::RequestModel(modelId, GAME_REQUIRED);
        CStreaming::LoadAllRequestedModels(false);
        if (CStreaming::ms_aInfoForModel[modelId].m_nLoadState != LOADSTATE_LOADED)
            return "null";
        // Restore deletable state before touching the model — the spawned vehicle
        // holds its own reference, so streaming won't evict a model in use.
        if (!(oldFlags & GAME_REQUIRED)) {
            CStreaming::SetModelIsDeletable(modelId);
            CStreaming::SetModelTxdIsDeletable(modelId);
        }

        auto* mi = CModelInfo::GetModelInfo(modelId);
        if (!mi || mi->GetModelType() != MODEL_INFO_VEHICLE) return "null";
        auto* vmi = reinterpret_cast<CVehicleModelInfo*>(mi);

        CVector pos = player->TransformFromObjectSpace({ ox, oy, -1.0f });
        float heading = player->GetHeading();

        CVehicle* veh = nullptr;
        switch (vmi->m_nVehicleType) {
            case VEHICLE_BMX:        veh = new CBmx(modelId, eVehicleCreatedBy::RANDOM_VEHICLE); break;
            case VEHICLE_BIKE:       veh = new CBike(modelId, eVehicleCreatedBy::RANDOM_VEHICLE); break;
            case VEHICLE_QUAD:       veh = new CQuadBike(modelId, eVehicleCreatedBy::RANDOM_VEHICLE); break;
            case VEHICLE_AUTOMOBILE: veh = new CAutomobile(modelId, eVehicleCreatedBy::RANDOM_VEHICLE, true); break;
            case VEHICLE_MTRUCK:     veh = new CMonsterTruck(modelId, eVehicleCreatedBy::RANDOM_VEHICLE); break;
            case VEHICLE_TRAILER:    veh = new CTrailer(modelId, eVehicleCreatedBy::RANDOM_VEHICLE); break;
            case VEHICLE_BOAT:       veh = new CBoat(modelId, eVehicleCreatedBy::RANDOM_VEHICLE); break;
            case VEHICLE_HELI:       veh = new CHeli(modelId, eVehicleCreatedBy::RANDOM_VEHICLE); break;
            case VEHICLE_PLANE:      veh = new CPlane(modelId, eVehicleCreatedBy::RANDOM_VEHICLE); break;
            case VEHICLE_TRAIN:
                veh = new CTrain(modelId, eVehicleCreatedBy::RANDOM_VEHICLE);
                reinterpret_cast<CTrain*>(veh)->m_nTrainFlags.bNotOnARailRoad = true;
                break;
            default: break;
        }
        if (!veh) return "null";

        pos.z += veh->GetHeightAboveRoad();
        veh->SetOrientation(0.f, 0.f, heading);
        veh->Teleport(pos);  // calls CWorld::Add internally
        veh->m_nStatus = eEntityStatus::STATUS_ABANDONED;
        veh->bEngineOn = false;
        veh->m_eDoorLock = DOORLOCK_UNLOCKED;

        switch (veh->m_nVehicleClass) {
            case VEHICLE_BIKE:
                reinterpret_cast<CBike*>(veh)->bOnSideStand = true;
                reinterpret_cast<CBike*>(veh)->PlaceOnRoadProperly();
                break;
            case VEHICLE_AUTOMOBILE:
                reinterpret_cast<CAutomobile*>(veh)->PlaceOnRoadProperly();
                break;
        }

        veh->UpdateRwMatrix();
        CTheScripts::ClearSpaceForMissionEntity(veh->GetPosition(), veh);
        veh->ProcessControl();

        int handle = VehicleToHandle(veh);
        std::ostringstream o;
        o << "{\"handle\":" << handle << ",\"modelId\":" << modelId << "}";
        return o.str();
    }

    // ---- vehicle_get ----
    if (cmd == "vehicle_get") {
        CVehicle* veh = jsonHasKey(json, "handle")
            ? HandleToVehicle((int)jsonGetNum(json, "handle"))
            : FindPlayerVehicle(0, false);
        if (!veh) return "null";
        std::ostringstream o;
        o << "{"
          << "\"handle\":" << VehicleToHandle(veh) << ","
          << "\"modelId\":" << veh->m_nModelIndex << ","
          << "\"health\":" << fmtf(veh->m_fHealth) << ","
          << "\"speed\":" << fmtf(veh->m_vecMoveSpeed.Magnitude() * 50.0f) << ","
          << "\"gear\":" << (int)veh->m_nCurrentGear << ","
          << "\"doorLock\":" << (int)veh->m_eDoorLock << ","
          << "\"engineOn\":" << (veh->bEngineOn ? "true" : "false") << ","
          << "\"driverHandle\":" << (veh->m_pDriver ? PedToHandle(veh->m_pDriver) : -1)
          << "}";
        return o.str();
    }

    // ---- vehicle_set ----
    if (cmd == "vehicle_set") {
        CVehicle* veh = jsonHasKey(json, "handle")
            ? HandleToVehicle((int)jsonGetNum(json, "handle"))
            : FindPlayerVehicle(0, false);
        if (!veh) return "null";
        if (jsonHasKey(json, "health"))   veh->m_fHealth = (float)jsonGetNum(json, "health");
        if (jsonHasKey(json, "doorLock")) veh->m_eDoorLock = (eDoorLock)(int)jsonGetNum(json, "doorLock");
        if (jsonHasKey(json, "engine"))   veh->bEngineOn = jsonGetBool(json, "engine");
        return "{}";
    }

    // ---- ped_spawn ----
    if (cmd == "ped_spawn") {
        CPlayerPed* player = FindPlayerPed(0);
        if (!player) return "null";
        int modelId = (int)jsonGetNum(json, "modelId");
        float ox = (float)jsonGetNum(json, "offsetX", 3.0);
        float oy = (float)jsonGetNum(json, "offsetY", 0.0);
        int pedType = (int)jsonGetNum(json, "pedType", 4);

        CStreaming::RequestModel(modelId, 0);
        CStreaming::LoadAllRequestedModels(false);
        if (!CStreaming::HasModelLoaded(modelId)) return "null";

        CVector pos = player->TransformFromObjectSpace({ ox, oy, -1.0f });
        pos.z += 1.0f;

        int safePedType = (pedType >= 0 && pedType <= 23) ? pedType : 4;
        auto* ped = new CCivilianPed((ePedType)safePedType, modelId);
        if (!ped) return "null";
        float heading = player->GetHeading() + plugin::DegToRad(180.0f);
        ped->SetPosition(pos.x, pos.y, pos.z);
        ped->SetHeading(heading);
        ped->m_fHeadingCurrent = ped->m_fHeadingGoal = heading;
        ped->GetMatrix().UpdateRW();

        if (ped->m_pRwClump)
            RpAnimBlendClumpUpdateAnimations(ped->m_pRwClump, CTimer::ms_fTimeStep);

        ped->m_fContactSurfaceBrightness = 0.5f;
        CWorld::ClearExcitingStuffFromArea(pos, 2.0f, false);
        CTheScripts::ClearSpaceForMissionEntity(ped->GetPosition(), ped);
        CWorld::Add(ped);
        CStreaming::SetModelIsDeletable(modelId);

        int handle = PedToHandle(ped);
        std::ostringstream o;
        o << "{\"handle\":" << handle << ",\"modelId\":" << modelId << "}";
        return o.str();
    }

    // ---- ped_get ----
    if (cmd == "ped_get") {
        CPed* ped = HandleToPed((int)jsonGetNum(json, "handle"));
        if (!ped) return "null";
        CVector pos = ped->GetPosition();
        std::ostringstream o;
        o << "{"
          << "\"x\":" << fmtf(pos.x) << ","
          << "\"y\":" << fmtf(pos.y) << ","
          << "\"z\":" << fmtf(pos.z) << ","
          << "\"health\":" << fmtf(ped->m_fHealth) << ","
          << "\"armour\":" << fmtf(ped->m_fArmour) << ","
          << "\"modelId\":" << ped->m_nModelIndex << ","
          << "\"alive\":" << (ped->IsAlive() ? "true" : "false")
          << "}";
        return o.str();
    }

    // ---- ped_set ----
    if (cmd == "ped_set") {
        CPed* ped = HandleToPed((int)jsonGetNum(json, "handle"));
        if (!ped) return "null";
        if (jsonHasKey(json, "health")) ped->m_fHealth = (float)jsonGetNum(json, "health");
        if (jsonHasKey(json, "armour")) ped->m_fArmour = (float)jsonGetNum(json, "armour");
        return "{}";
    }

    // ---- ped_set_task ----
    if (cmd == "ped_set_task") {
        CPed* ped = HandleToPed((int)jsonGetNum(json, "handle"));
        if (!ped || !ped->m_pIntelligence) return "null";
        std::string task = jsonGetStr(json, "task");
        CPlayerPed* player = FindPlayerPed(0);

        CTask* t = nullptr;
        if (task == "wander") {
            t = new CTaskComplexWanderStandard(PEDMOVE_WALK, 0, true);
            ped->m_pIntelligence->m_TaskMgr.SetTask(t, TASK_PRIMARY_DEFAULT, true);
        } else if (task == "follow_player" && player) {
            // CTaskComplexFollowPedFootstep not in SDK — wander fast toward player area
            t = new CTaskComplexWanderStandard(PEDMOVE_RUN, 0, true);
            ped->m_pIntelligence->m_TaskMgr.SetTask(t, TASK_PRIMARY_DEFAULT, true);
        } else if (task == "attack_player" && player) {
            t = new CTaskComplexKillPedOnFoot(player, -1, 0, 0, 100, 0);
            ped->m_pIntelligence->m_TaskMgr.SetTask(t, TASK_PRIMARY_PRIMARY, true);
        } else if (task == "flee_player" && player) {
            t = new CTaskComplexWanderStandard(PEDMOVE_RUN, 0, true);
            ped->m_pIntelligence->m_TaskMgr.SetTask(t, TASK_PRIMARY_PRIMARY, true);
        } else if (task == "hands_up") {
            t = new CTaskSimpleStandStill(60000, false, false, 4.0f);
            ped->m_pIntelligence->m_TaskMgr.SetTask(t, TASK_PRIMARY_PRIMARY, true);
        } else if (task == "die") {
            t = new CTaskComplexDie(WEAPONTYPE_UNARMED, 0, ANIM_DEFAULT_KO_SKID_FRONT, 4.0f, 1.0f, false, false, 0, false);
            ped->m_pIntelligence->m_TaskMgr.SetTask(t, TASK_PRIMARY_PRIMARY, true);
        } else {
            return "{\"error\":\"unknown task or missing player\"}";
        }
        return "{}";
    }

    // ---- world_set_time ----
    if (cmd == "world_set_time") {
        int hour = (int)jsonGetNum(json, "hour");
        int minute = (int)jsonGetNum(json, "minute", 0);
        CClock::ms_nGameClockHours = (unsigned char)hour;
        CClock::ms_nGameClockMinutes = (unsigned char)minute;
        return "{}";
    }

    // ---- world_get_time ----
    if (cmd == "world_get_time") {
        std::ostringstream o;
        o << "{\"hour\":" << (int)CClock::ms_nGameClockHours
          << ",\"minute\":" << (int)CClock::ms_nGameClockMinutes << "}";
        return o.str();
    }

    // ---- world_set_weather ----
    if (cmd == "world_set_weather") {
        int wid = (int)jsonGetNum(json, "weatherId");
        CWeather::ForceWeatherNow((short)wid);
        return "{}";
    }

    // ---- world_pool_dump ----
    if (cmd == "world_pool_dump") {
        if (!CPools::ms_pPedPool || !CPools::ms_pVehiclePool) return "null";
        float radius = (float)jsonGetNum(json, "radius", 50.0);
        CPlayerPed* player = FindPlayerPed(0);
        CVector origin = player ? player->GetPosition() : CVector(0, 0, 0);
        float r2 = radius * radius;

        std::ostringstream o;
        o << "{\"peds\":[";
        bool first = true;
        for (auto* ped : CPools::ms_pPedPool) {
            if (ped == (CPed*)player) continue;
            CVector d = ped->GetPosition() - origin;
            if (d.x*d.x + d.y*d.y + d.z*d.z > r2) continue;
            CVector p = ped->GetPosition();
            if (!first) o << ",";
            o << "{\"handle\":" << PedToHandle(ped)
              << ",\"modelId\":" << ped->m_nModelIndex
              << ",\"x\":" << fmtf(p.x)
              << ",\"y\":" << fmtf(p.y)
              << ",\"z\":" << fmtf(p.z)
              << ",\"health\":" << fmtf(ped->m_fHealth)
              << "}";
            first = false;
        }
        o << "],\"vehicles\":[";
        first = true;
        for (auto* veh : CPools::ms_pVehiclePool) {
            CVector d = veh->GetPosition() - origin;
            if (d.x*d.x + d.y*d.y + d.z*d.z > r2) continue;
            CVector p = veh->GetPosition();
            if (!first) o << ",";
            o << "{\"handle\":" << VehicleToHandle(veh)
              << ",\"modelId\":" << veh->m_nModelIndex
              << ",\"x\":" << fmtf(p.x)
              << ",\"y\":" << fmtf(p.y)
              << ",\"z\":" << fmtf(p.z)
              << ",\"health\":" << fmtf(veh->m_fHealth)
              << "}";
            first = false;
        }
        o << "]}";
        return o.str();
    }

    // ---- memory_read ----
    if (cmd == "memory_read") {
        uintptr_t addr = (uintptr_t)jsonGetNum(json, "address");
        int size = std::min((int)jsonGetNum(json, "size", 4), 1024);
        std::string type = jsonGetStr(json, "type", "hex");

        if (IsBadReadPtr(reinterpret_cast<void*>(addr), (size_t)size))
            return "null";

        if (type == "int32" && size >= 4) {
            int v = *reinterpret_cast<int*>(addr);
            return "{\"value\":" + std::to_string(v) + "}";
        }
        if (type == "float" && size >= 4) {
            float v = *reinterpret_cast<float*>(addr);
            return "{\"value\":" + fmtf(v) + "}";
        }
        if (type == "string") {
            char buf[1025] = {};
            memcpy(buf, reinterpret_cast<void*>(addr), size);
            return "{\"value\":" + jsonStr(std::string(buf, strnlen(buf, size))) + "}";
        }
        // hex
        std::ostringstream o;
        o << "{\"value\":\"";
        unsigned char* p = reinterpret_cast<unsigned char*>(addr);
        for (int i = 0; i < size; i++) {
            if (i) o << " ";
            o << std::hex << std::setw(2) << std::setfill('0') << (int)p[i];
        }
        o << "\"}";
        return o.str();
    }

    // ---- memory_write ----
    if (cmd == "memory_write") {
        uintptr_t addr = (uintptr_t)jsonGetNum(json, "address");
        std::string type = jsonGetStr(json, "type");
        if (IsBadWritePtr(reinterpret_cast<void*>(addr), 8))
            return "null";
        DWORD old; VirtualProtect(reinterpret_cast<void*>(addr), 8, PAGE_EXECUTE_READWRITE, &old);
        if (type == "int32") {
            *reinterpret_cast<int*>(addr) = (int)jsonGetNum(json, "value");
        } else if (type == "float") {
            *reinterpret_cast<float*>(addr) = (float)jsonGetNum(json, "value");
        } else if (type == "bytes") {
            std::string hex = jsonGetStr(json, "value");
            std::istringstream ss(hex);
            std::string tok; int off = 0;
            while (ss >> tok && off < 256) {
                reinterpret_cast<unsigned char*>(addr)[off++] = (unsigned char)strtol(tok.c_str(), nullptr, 16);
            }
        }
        VirtualProtect(reinterpret_cast<void*>(addr), 8, old, &old);
        return "{}";
    }

    // ---- plugin_log_read ----
    if (cmd == "plugin_log_read") {
        std::string name = jsonGetStr(json, "pluginName");
        bool clear = jsonGetBool(json, "clear", false);
        if (name.empty()) return "null";

        HMODULE hMod = GetModuleHandleA(name.c_str());
        if (!hMod) return "{\"error\":\"module not loaded\"}";

        char*  pLog = reinterpret_cast<char*>(GetProcAddress(hMod, "GtaMcpDebugLog"));
        int*   pLen = reinterpret_cast<int*>(GetProcAddress(hMod, "GtaMcpDebugLen"));
        if (!pLog || !pLen) return "{\"error\":\"GtaMcpDebugLog/GtaMcpDebugLen not exported\"}";

        std::string log(pLog, std::min(*pLen, 4096));
        if (clear) { memset(pLog, 0, 4096); *pLen = 0; }
        return "{\"log\":" + jsonStr(log) + "}";
    }

    return "{\"error\":\"unknown command\"}";
}

// ---- pipe server (runs on dedicated thread) ------------------------------

static HANDLE g_pipe = INVALID_HANDLE_VALUE;
static bool   g_running = false;

static void pipeThread() {
    while (g_running) {
        g_pipe = CreateNamedPipeA(
            "\\\\.\\pipe\\gtasa_mcp_bridge",
            PIPE_ACCESS_DUPLEX,
            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
            1, 65536, 65536, 0, nullptr
        );
        if (g_pipe == INVALID_HANDLE_VALUE) { Sleep(1000); continue; }

        if (!ConnectNamedPipe(g_pipe, nullptr) && GetLastError() != ERROR_PIPE_CONNECTED) {
            CloseHandle(g_pipe);
            g_pipe = INVALID_HANDLE_VALUE;
            continue;
        }

        std::string buf;
        char chunk[4096];
        while (g_running) {
            DWORD avail = 0;
            if (!PeekNamedPipe(g_pipe, nullptr, 0, nullptr, &avail, nullptr))
                break; // client gone or pipe broken — reconnect

            // Flush pending responses every iteration so replies are never
            // stuck waiting for the next incoming message.
            {
                std::lock_guard<std::mutex> lk(g_mutex);
                while (!g_respQueue.empty()) {
                    std::string line = g_respQueue.front().line + "\n";
                    g_respQueue.pop_front();
                    DWORD w;
                    WriteFile(g_pipe, line.c_str(), (DWORD)line.size(), &w, nullptr);
                }
            }

            if (avail == 0) { Sleep(8); continue; } // idle — keep connection alive

            DWORD read = 0;
            DWORD toRead = std::min(avail, (DWORD)sizeof(chunk));
            if (!ReadFile(g_pipe, chunk, toRead, &read, nullptr) || read == 0) break;

            buf.append(chunk, read);

            // Dispatch complete lines as commands
            size_t pos;
            while ((pos = buf.find('\n')) != std::string::npos) {
                std::string line = buf.substr(0, pos);
                buf.erase(0, pos + 1);
                if (line.empty()) continue;

                int id = (int)jsonGetNum(line, "id");
                std::string cmd = jsonGetStr(line, "cmd");

                // Scene detection reads only static globals — safe on this thread
                // even during splash, menu, pause, or loading.
                bool menuActive = FrontEndMenuManager.m_bMenuActive;
                bool playerReady = CWorld::Players[0].m_pPed != nullptr;
                const char* scene = menuActive ? (playerReady ? "paused" : "menu")
                                               : (playerReady ? "in_game" : "loading");

                if (cmd == "game_state") {
                    // Answered here so scene queries work before gameplay starts.
                    std::ostringstream o;
                    o << "{\"id\":" << id << ",\"ok\":true,\"data\":{\"scene\":\"" << scene
                      << "\",\"menuVisible\":" << (menuActive ? "true" : "false")
                      << ",\"playerReady\":" << (playerReady ? "true" : "false") << "}}";
                    std::lock_guard<std::mutex> lk(g_mutex);
                    g_respQueue.push_back({ o.str() });
                } else if (std::string(scene) != "in_game") {
                    // Don't queue gameplay commands while not in gameplay — they'd
                    // either hang until timeout or execute stale after loading.
                    std::ostringstream o;
                    o << "{\"id\":" << id << ",\"ok\":false,\"error\":\"game not in gameplay (scene: "
                      << scene << ")\"}";
                    std::lock_guard<std::mutex> lk(g_mutex);
                    g_respQueue.push_back({ o.str() });
                } else {
                    std::lock_guard<std::mutex> lk(g_mutex);
                    g_cmdQueue.push_back({ id, cmd, line });
                }
            }
        }

        DisconnectNamedPipe(g_pipe);
        CloseHandle(g_pipe);
        g_pipe = INVALID_HANDLE_VALUE;
    }
}

// ---- plugin entry --------------------------------------------------------

using namespace plugin;
using namespace injector;

class DebugBridge {
public:
    DebugBridge() {
        // Version guard — SA 1.0 US only (Compact + Hoodlum)
        {
            injector::game_version_manager gvm;
            gvm.PluginName = "debug_bridge";
            if (!gvm.Detect() || !gvm.IsSA() || !gvm.IsUS() ||
                gvm.GetMajorVersion() != 1 || gvm.GetMinorVersion() != 0) {
                gvm.RaiseIncompatibleVersion();
                return;
            }
        }

        // Check sentinel file — if absent, stay dormant
        char gameDir[MAX_PATH];
        GetModuleFileNameA(nullptr, gameDir, MAX_PATH);
        std::string dir(gameDir);
        dir = dir.substr(0, dir.rfind('\\'));
        std::string sentinel = dir + "\\mcp_debug.lock";

        if (GetFileAttributesA(sentinel.c_str()) == INVALID_FILE_ATTRIBUTES)
            return; // not in MCP debug mode — zero overhead

        g_running = true;
        std::thread(pipeThread).detach();

        Events::gameProcessEvent += [] {
            std::lock_guard<std::mutex> lk(g_mutex);
            while (!g_cmdQueue.empty()) {
                BridgeCommand c = g_cmdQueue.front();
                g_cmdQueue.pop_front();

                std::string result;
                bool ok = true;
                try {
                    result = handleCommand(c.cmd, c.json);
                } catch (const std::exception& e) {
                    result = jsonStr(e.what());
                    ok = false;
                }

                std::ostringstream resp;
                if (ok && result != "null") {
                    resp << "{\"id\":" << c.id << ",\"ok\":true,\"data\":" << result << "}";
                } else if (result == "null") {
                    resp << "{\"id\":" << c.id << ",\"ok\":false,\"error\":\"not found or game not ready\"}";
                } else {
                    resp << "{\"id\":" << c.id << ",\"ok\":false,\"error\":" << result << "}";
                }
                g_respQueue.push_back({ resp.str() });
            }
        };
    }

    ~DebugBridge() {
        g_running = false;
        if (g_pipe != INVALID_HANDLE_VALUE) {
            DisconnectNamedPipe(g_pipe);
            CloseHandle(g_pipe);
        }
    }
} debugBridge;
