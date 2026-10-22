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
#include <CObject.h>
#include <CPickups.h>
#include <CFireManager.h>
#include <CAudioEngine.h>
#include <CCheat.h>
#include <CStats.h>
#include <CCamera.h>
#include <CStreaming.h>
#include <CModelInfo.h>
#include <CTheScripts.h>
#include <CRunningScript.h>
#include <CWanted.h>
#include <CTaskComplexWanderStandard.h>
#include <CTaskComplexKillPedOnFoot.h>
#include <CTaskComplexEnterCarAsDriver.h>
#include <CTaskSimpleRunNamedAnim.h>
#include <CTaskSimpleDuck.h>
#include <CTaskSimpleJetPack.h>
#include <CExplosion.h>
#include <CWeather.h>
#include <CTaskSimpleStandStill.h>
#include <CTaskComplexDie.h>
#include <CMenuManager.h>
#include <CCutsceneMgr.h>
#include <CRadar.h>
#include <extensions/PoolIterator.h>
#include <extensions/ScriptCommands.h>
#include <CWeaponInfo.h>
#include <enums/eWeaponType.h>
#include <enums/eTaskType.h>
#include <enums/eAnimations.h>
#include <enums/eScriptCommands.h>
#include <enums/eStats.h>
#include <enums/eRadioID.h>
#include <enums/eCamMode.h>
#include <Patch.h>

#include <windows.h>
#include <string>
#include <deque>
#include <vector>
#include <mutex>
#include <thread>
#include <sstream>
#include <iomanip>
#include <cstdio>
#include <cstdarg>
#include <algorithm>

static void BridgeLog(const char* fmt, ...) {
    static FILE* f = nullptr;
    if (!f) {
        char dir[MAX_PATH] = {};
        GetModuleFileNameA(nullptr, dir, MAX_PATH);
        char* slash = strrchr(dir, '\\');
        if (slash) *slash = '\0';
        char path[MAX_PATH] = {};
        snprintf(path, sizeof(path), "%s\\debug_bridge.log", dir);
        f = fopen(path, "w");
        if (!f) return;
    }
    SYSTEMTIME st;
    GetLocalTime(&st);
    fprintf(f, "[%02d:%02d:%02d.%03d] ", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
    va_list ap;
    va_start(ap, fmt);
    vfprintf(f, fmt, ap);
    va_end(ap);
    fputc('\n', f);
    fflush(f);
}

// ---- injected-key state (processed per frame on the game thread) ----------

struct PendingKey { int vk; int framesLeft; };
static std::vector<PendingKey> g_pendingKeys;

// ---- fast-boot -------------------------------------------------------------
static bool g_fastboot = false;
static int  g_fastbootTimer = 0;
static const ULONGLONG kQueueTtlMs = 15000;

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

// ---- minimal JSON parser ------------------------------------------------

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

struct BridgeCommand { int id; std::string cmd; std::string json; ULONGLONG receivedAtMs; };
struct Response { std::string line; };

static std::deque<BridgeCommand> g_cmdQueue;
static std::deque<Response>      g_respQueue;
static std::mutex                g_mutex;

// ---- handle maps (ped, vehicle, object) ----------------------------------

static CPed* HandleToPed(int handle) {
    if (!CPools::ms_pPedPool) return nullptr;
    return CPools::ms_pPedPool->GetAtRef(handle);
}
static CVehicle* HandleToVehicle(int handle) {
    if (!CPools::ms_pVehiclePool) return nullptr;
    return CPools::ms_pVehiclePool->GetAtRef(handle);
}
static CObject* HandleToObject(int handle) {
    if (!CPools::ms_pObjectPool) return nullptr;
    return CPools::ms_pObjectPool->GetAtRef(handle);
}
static int PedToHandle(CPed* ped) {
    return CPools::ms_pPedPool ? CPools::ms_pPedPool->GetRef(ped) : -1;
}
static int VehicleToHandle(CVehicle* veh) {
    return CPools::ms_pVehiclePool ? CPools::ms_pVehiclePool->GetRef(veh) : -1;
}
static int ObjectToHandle(CObject* obj) {
    return CPools::ms_pObjectPool ? CPools::ms_pObjectPool->GetRef(obj) : -1;
}

// ---- dynamic SCM byte emitter -------------------------------------------

struct DynScriptCode {
    unsigned char data[512];
    unsigned int size;

    DynScriptCode(short commandId) : size(0) {
        memcpy(&data[size], &commandId, 2);
        size += 2;
    }

    void AddInt(int val) {
        data[size++] = (unsigned char)SCRIPTPARAM_STATIC_INT_32BITS;
        memcpy(&data[size], &val, 4);
        size += 4;
    }

    void AddFloat(float val) {
        data[size++] = (unsigned char)SCRIPTPARAM_STATIC_FLOAT;
        memcpy(&data[size], &val, 4);
        size += 4;
    }

    void AddString(const char* str) {
        data[size++] = (unsigned char)SCRIPTPARAM_STATIC_PASCAL_STRING;
        unsigned int len = (unsigned int)strlen(str);
        data[size++] = (unsigned char)len;
        memcpy(&data[size], str, len);
        size += len;
    }

    void AddPed(CPed* ped) {
        int handle = CPools::ms_pPedPool ? CPools::ms_pPedPool->GetRef(ped) : 0;
        AddInt(handle);
    }

    void AddVehicle(CVehicle* veh) {
        int handle = CPools::ms_pVehiclePool ? CPools::ms_pVehiclePool->GetRef(veh) : 0;
        AddInt(handle);
    }

    void AddObject(CObject* obj) {
        int handle = CPools::ms_pObjectPool ? CPools::ms_pObjectPool->GetRef(obj) : 0;
        AddInt(handle);
    }
};

// ---- command handlers (run on game thread via gameProcessEvent) ----------

static std::string handleCommand(const std::string& cmd, const std::string& json) {
    // ==========================================
    // PLAYER
    // ==========================================
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
          << "\"maxHealth\":" << fmtf(player->m_fMaxHealth) << ","
          << "\"wanted\":" << (player->m_pPlayerData && player->m_pPlayerData->m_pWanted ? (int)player->m_pPlayerData->m_pWanted->m_nWantedLevel : 0) << ","
          << "\"money\":" << CWorld::Players[0].m_nMoney << ","
          << "\"interior\":" << (int)player->m_nAreaCode << ","
          << "\"inVehicle\":" << (player->bInVehicle ? "true" : "false") << ","
          << "\"vehicleHandle\":" << (veh ? VehicleToHandle(veh) : -1)
          << "}";
        return o.str();
    }

    if (cmd == "player_set") {
        CPlayerPed* player = FindPlayerPed(0);
        if (!player) return "null";
        if (jsonHasKey(json, "health")) player->m_fHealth = (float)jsonGetNum(json, "health");
        if (jsonHasKey(json, "maxHealth")) player->m_fMaxHealth = (float)jsonGetNum(json, "maxHealth");
        if (jsonHasKey(json, "armour")) player->m_fArmour = (float)jsonGetNum(json, "armour");
        if (jsonHasKey(json, "money"))  CWorld::Players[0].m_nMoney = (int)jsonGetNum(json, "money");
        if (jsonHasKey(json, "wanted") && player->m_pPlayerData && player->m_pPlayerData->m_pWanted) {
            int w = (int)jsonGetNum(json, "wanted");
            player->m_pPlayerData->m_pWanted->SetWantedLevel(w);
        }
        if (jsonHasKey(json, "invincible")) {
            bool inv = jsonGetBool(json, "invincible");
            player->bInvulnerable = inv;
        }
        return "{}";
    }

    if (cmd == "player_teleport") {
        CPlayerPed* player = FindPlayerPed(0);
        if (!player) return "null";
        float x = (float)jsonGetNum(json, "x");
        float y = (float)jsonGetNum(json, "y");
        float z = (float)jsonGetNum(json, "z");
        CVector pos = { x, y, z };
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

    if (cmd == "player_give_weapon") {
        CPlayerPed* player = FindPlayerPed(0);
        if (!player) return "null";
        int wt = (int)jsonGetNum(json, "weaponType");
        int ammo = (int)jsonGetNum(json, "ammo", 100);
        bool setActive = jsonGetBool(json, "setActive", true);
        CWeaponInfo* wi = CWeaponInfo::GetWeaponInfo((eWeaponType)wt);
        if (wi && wi->m_nModelId > 0) {
            CStreaming::RequestModel(wi->m_nModelId, GAME_REQUIRED);
            CStreaming::LoadAllRequestedModels(false);
            CStreaming::SetModelIsDeletable(wi->m_nModelId);
        }
        player->GiveWeapon((eWeaponType)wt, ammo, true);
        if (setActive) player->SetCurrentWeapon((eWeaponType)wt);
        return "{}";
    }

    if (cmd == "player_remove_weapon") {
        CPlayerPed* player = FindPlayerPed(0);
        if (!player) return "null";
        int wt = (int)jsonGetNum(json, "weaponType");
        player->ClearWeapon((eWeaponType)wt);
        return "{}";
    }

    if (cmd == "player_clear_weapons") {
        CPlayerPed* player = FindPlayerPed(0);
        if (!player) return "null";
        player->ClearWeapons();
        return "{}";
    }

    if (cmd == "player_set_skin") {
        CPlayerPed* player = FindPlayerPed(0);
        if (!player) return "null";
        int modelId = (int)jsonGetNum(json, "modelId");
        CStreaming::RequestModel(modelId, GAME_REQUIRED);
        CStreaming::LoadAllRequestedModels(false);
        if (!CStreaming::HasModelLoaded(modelId)) return "{\"error\":\"model failed to load\"}";
        player->SetModelIndex(modelId);
        CStreaming::SetModelIsDeletable(modelId);
        return "{}";
    }

    if (cmd == "player_set_stat") {
        int statId = (int)jsonGetNum(json, "statId");
        float val = (float)jsonGetNum(json, "value");
        CStats::SetStatValue((unsigned short)statId, val);
        return "{}";
    }

    if (cmd == "player_get_stat") {
        int statId = (int)jsonGetNum(json, "statId");
        float val = CStats::GetStatValue((unsigned short)statId);
        return "{\"statId\":" + std::to_string(statId) + ",\"value\":" + fmtf(val) + "}";
    }

    if (cmd == "player_clear_wanted") {
        CPlayerPed* player = FindPlayerPed(0);
        if (!player || !player->m_pPlayerData || !player->m_pPlayerData->m_pWanted) return "null";
        player->m_pPlayerData->m_pWanted->SetWantedLevel(0);
        return "{}";
    }

    if (cmd == "player_enter_vehicle") {
        CPlayerPed* player = FindPlayerPed(0);
        if (!player) return "null";
        CVehicle* veh = HandleToVehicle((int)jsonGetNum(json, "vehicleHandle"));
        if (!veh) return "null";
        plugin::scripting::CallCommandById((unsigned int)COMMAND_TASK_ENTER_CAR_AS_DRIVER, player, veh);
        return "{}";
    }

    // ==========================================
    // VEHICLE
    // ==========================================
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
        veh->Teleport(pos);
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
          << "\"primaryColor\":" << (int)veh->m_nPrimaryColor << ","
          << "\"secondaryColor\":" << (int)veh->m_nSecondaryColor << ","
          << "\"engineOn\":" << (veh->bEngineOn ? "true" : "false") << ","
          << "\"driverHandle\":" << (veh->m_pDriver ? PedToHandle(veh->m_pDriver) : -1)
          << "}";
        return o.str();
    }

    if (cmd == "vehicle_set") {
        CVehicle* veh = jsonHasKey(json, "handle")
            ? HandleToVehicle((int)jsonGetNum(json, "handle"))
            : FindPlayerVehicle(0, false);
        if (!veh) return "null";
        if (jsonHasKey(json, "health"))   veh->m_fHealth = (float)jsonGetNum(json, "health");
        if (jsonHasKey(json, "doorLock")) veh->m_eDoorLock = (eDoorLock)(int)jsonGetNum(json, "doorLock");
        if (jsonHasKey(json, "engine"))   veh->bEngineOn = jsonGetBool(json, "engine");
        if (jsonHasKey(json, "dirtLevel")) veh->m_fDirtLevel = (float)jsonGetNum(json, "dirtLevel");
        if (jsonHasKey(json, "alarm")) veh->m_nAlarmState = (unsigned short)jsonGetNum(json, "alarm");
        return "{}";
    }

    if (cmd == "vehicle_set_color") {
        CVehicle* veh = jsonHasKey(json, "handle")
            ? HandleToVehicle((int)jsonGetNum(json, "handle"))
            : FindPlayerVehicle(0, false);
        if (!veh) return "null";
        int col1 = (int)jsonGetNum(json, "primaryColor");
        int col2 = (int)jsonGetNum(json, "secondaryColor", col1);
        veh->m_nPrimaryColor = (unsigned char)col1;
        veh->m_nSecondaryColor = (unsigned char)col2;
        return "{}";
    }

    if (cmd == "vehicle_tune") {
        CVehicle* veh = jsonHasKey(json, "handle")
            ? HandleToVehicle((int)jsonGetNum(json, "handle"))
            : FindPlayerVehicle(0, false);
        if (!veh) return "null";
        int modId = (int)jsonGetNum(json, "upgradeModelId");
        CStreaming::RequestModel(modId, GAME_REQUIRED);
        CStreaming::LoadAllRequestedModels(false);
        plugin::scripting::CallCommandById((unsigned int)COMMAND_ADD_VEHICLE_MOD, veh, modId);
        CStreaming::SetModelIsDeletable(modId);
        return "{}";
    }

    if (cmd == "vehicle_pop_tire") {
        CVehicle* veh = jsonHasKey(json, "handle")
            ? HandleToVehicle((int)jsonGetNum(json, "handle"))
            : FindPlayerVehicle(0, false);
        if (!veh) return "null";
        int tire = (int)jsonGetNum(json, "tireIndex", 0);
        veh->BurstTyre((unsigned char)tire, true);
        return "{}";
    }

    if (cmd == "vehicle_set_door_state") {
        CVehicle* veh = jsonHasKey(json, "handle")
            ? HandleToVehicle((int)jsonGetNum(json, "handle"))
            : FindPlayerVehicle(0, false);
        if (!veh) return "null";
        int doorId = (int)jsonGetNum(json, "doorId");
        float ratio = (float)jsonGetNum(json, "ratio", 1.0f);
        veh->OpenDoor(nullptr, doorId, (eDoors)doorId, ratio, true);
        return "{}";
    }

    if (cmd == "vehicle_remove") {
        CVehicle* veh = jsonHasKey(json, "handle")
            ? HandleToVehicle((int)jsonGetNum(json, "handle"))
            : FindPlayerVehicle(0, false);
        if (!veh) return "null";
        if (veh == FindPlayerVehicle(0, false)) return "{\"error\":\"cannot remove the vehicle CJ is in\"}";
        CWorld::Remove(veh);
        delete veh;
        return "{}";
    }

    if (cmd == "vehicle_teleport") {
        CVehicle* veh = jsonHasKey(json, "handle")
            ? HandleToVehicle((int)jsonGetNum(json, "handle"))
            : FindPlayerVehicle(0, false);
        if (!veh) return "null";
        CVector pos = { (float)jsonGetNum(json, "x"), (float)jsonGetNum(json, "y"), (float)jsonGetNum(json, "z") };
        pos.z += veh->GetHeightAboveRoad();
        veh->Teleport(pos, false);
        return "{}";
    }

    if (cmd == "vehicle_fix") {
        CVehicle* veh = jsonHasKey(json, "handle")
            ? HandleToVehicle((int)jsonGetNum(json, "handle"))
            : FindPlayerVehicle(0, false);
        if (!veh) return "null";
        veh->m_fHealth = 1000.0f;
        veh->SetOrientation(0.0f, 0.0f, veh->GetHeading());
        veh->UpdateRwMatrix();
        veh->Fix();
        return "{}";
    }

    // ==========================================
    // PED
    // ==========================================
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

    if (cmd == "ped_set") {
        CPed* ped = HandleToPed((int)jsonGetNum(json, "handle"));
        if (!ped) return "null";
        if (jsonHasKey(json, "health")) ped->m_fHealth = (float)jsonGetNum(json, "health");
        if (jsonHasKey(json, "armour")) ped->m_fArmour = (float)jsonGetNum(json, "armour");
        return "{}";
    }

    if (cmd == "ped_remove") {
        CPed* ped = HandleToPed((int)jsonGetNum(json, "handle"));
        if (!ped) return "null";
        if (ped == FindPlayerPed(0)) return "{\"error\":\"cannot remove the player\"}";
        CWorld::Remove(ped);
        delete ped;
        return "{}";
    }

    if (cmd == "ped_teleport") {
        CPed* ped = HandleToPed((int)jsonGetNum(json, "handle"));
        if (!ped) return "null";
        CVector pos = { (float)jsonGetNum(json, "x"), (float)jsonGetNum(json, "y"), (float)jsonGetNum(json, "z") };
        ped->Teleport(pos, false);
        return "{}";
    }

    if (cmd == "ped_give_weapon") {
        CPed* ped = HandleToPed((int)jsonGetNum(json, "handle"));
        if (!ped) return "null";
        int wt = (int)jsonGetNum(json, "weaponType");
        int ammo = (int)jsonGetNum(json, "ammo", 100);
        bool setActive = jsonGetBool(json, "setActive", true);

        CWeaponInfo* wi = CWeaponInfo::GetWeaponInfo((eWeaponType)wt);
        if (!wi || wi->m_nModelId <= 0) return "{\"error\":\"invalid weaponType\"}";
        int modelId = wi->m_nModelId;
        unsigned char oldFlags = CStreaming::ms_aInfoForModel[modelId].m_nFlags;
        CStreaming::RequestModel(modelId, GAME_REQUIRED);
        CStreaming::LoadAllRequestedModels(false);
        if (CStreaming::ms_aInfoForModel[modelId].m_nLoadState != LOADSTATE_LOADED)
            return "{\"error\":\"weapon model failed to load\"}";
        if (!(oldFlags & GAME_REQUIRED)) {
            CStreaming::SetModelIsDeletable(modelId);
            CStreaming::SetModelTxdIsDeletable(modelId);
        }

        ped->GiveWeapon((eWeaponType)wt, ammo, true);
        if (setActive) ped->SetCurrentWeapon((eWeaponType)wt);
        return "{}";
    }

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
        } else if (task == "enter_vehicle" && jsonHasKey(json, "vehicleHandle")) {
            CVehicle* veh = HandleToVehicle((int)jsonGetNum(json, "vehicleHandle"));
            if (!veh) return "null";
            plugin::scripting::CallCommandById((unsigned int)COMMAND_TASK_ENTER_CAR_AS_DRIVER, ped, veh);
        } else if (task == "leave_vehicle") {
            CVehicle* veh = FindPlayerVehicle(0, false);
            if (veh) plugin::scripting::CallCommandById((unsigned int)COMMAND_TASK_LEAVE_ANY_CAR, ped);
        } else if (task == "drive_to") {
            float tx = (float)jsonGetNum(json, "x");
            float ty = (float)jsonGetNum(json, "y");
            float tz = (float)jsonGetNum(json, "z");
            float spd = (float)jsonGetNum(json, "speed", 20.0);
            CVehicle* veh = FindPlayerVehicle(0, false);
            if (veh) plugin::scripting::CallCommandById((unsigned int)COMMAND_TASK_CAR_DRIVE_TO_COORD, ped, veh, tx, ty, tz, spd, 0, 0, 0);
        } else if (task == "duck") {
            t = new CTaskSimpleDuck(DUCK_STANDALONE, 10000);
            ped->m_pIntelligence->m_TaskMgr.SetTask(t, TASK_PRIMARY_PRIMARY, true);
        } else if (task == "jetpack") {
            t = new CTaskSimpleJetPack();
            ped->m_pIntelligence->m_TaskMgr.SetTask(t, TASK_PRIMARY_PRIMARY, true);
        } else if (task == "play_anim") {
            std::string animName = jsonGetStr(json, "animName");
            std::string animLib  = jsonGetStr(json, "animLib", "ped");
            int animTime = (int)jsonGetNum(json, "animTime", 5000);
            if (animName.empty()) return "{\"error\":\"animName required\"}";
            t = new CTaskSimpleRunNamedAnim(animName.c_str(), animLib.c_str(), 0, 4.0f, animTime, false, false, false, false);
            ped->m_pIntelligence->m_TaskMgr.SetTask(t, TASK_PRIMARY_PRIMARY, true);
        } else if (task == "die") {
            t = new CTaskComplexDie(WEAPONTYPE_UNARMED, 0, ANIM_DEFAULT_KO_SKID_FRONT, 4.0f, 1.0f, false, false, 0, false);
            ped->m_pIntelligence->m_TaskMgr.SetTask(t, TASK_PRIMARY_PRIMARY, true);
        } else {
            return "{\"error\":\"unknown task or missing parameters\"}";
        }
        return "{}";
    }

    if (cmd == "ped_clear_tasks") {
        CPed* ped = HandleToPed((int)jsonGetNum(json, "handle"));
        if (!ped || !ped->m_pIntelligence) return "null";
        ped->m_pIntelligence->m_TaskMgr.ClearTaskEventResponse();
        plugin::scripting::CallCommandById((unsigned int)COMMAND_CLEAR_CHAR_TASKS, ped);
        return "{}";
    }

    if (cmd == "ped_resurrect") {
        CPed* ped = HandleToPed((int)jsonGetNum(json, "handle"));
        if (!ped) return "null";
        ped->m_fHealth = 100.0f;
        ped->SetPedState(PEDSTATE_IDLE);
        return "{}";
    }

    // ==========================================
    // CAMERA
    // ==========================================
    if (cmd == "camera_get") {
        CVector pos = TheCamera.GetPosition();
        CVector front = TheCamera.m_vecGameCamPos;
        std::ostringstream o;
        o << "{"
          << "\"x\":" << fmtf(pos.x) << ","
          << "\"y\":" << fmtf(pos.y) << ","
          << "\"z\":" << fmtf(pos.z) << ","
          << "\"frontX\":" << fmtf(front.x) << ","
          << "\"frontY\":" << fmtf(front.y) << ","
          << "\"frontZ\":" << fmtf(front.z) << ","
          << "\"fov\":" << fmtf(TheCamera.FindCamFOV()) << ","
          << "\"activeCam\":" << (int)TheCamera.m_nActiveCam
          << "}";
        return o.str();
    }

    if (cmd == "camera_set_pos") {
        float x = (float)jsonGetNum(json, "x");
        float y = (float)jsonGetNum(json, "y");
        float z = (float)jsonGetNum(json, "z");
        plugin::scripting::CallCommandById((unsigned int)COMMAND_SET_FIXED_CAMERA_POSITION, x, y, z, 0.0f, 0.0f, 0.0f);
        if (jsonHasKey(json, "targetX") && jsonHasKey(json, "targetY") && jsonHasKey(json, "targetZ")) {
            float tx = (float)jsonGetNum(json, "targetX");
            float ty = (float)jsonGetNum(json, "targetY");
            float tz = (float)jsonGetNum(json, "targetZ");
            plugin::scripting::CallCommandById((unsigned int)COMMAND_POINT_CAMERA_AT_POINT, tx, ty, tz, 2);
        }
        return "{}";
    }

    if (cmd == "camera_point_at") {
        if (jsonHasKey(json, "pedHandle")) {
            CPed* ped = HandleToPed((int)jsonGetNum(json, "pedHandle"));
            if (ped) plugin::scripting::CallCommandById((unsigned int)COMMAND_POINT_CAMERA_AT_CHAR, ped, 2, 2);
        } else if (jsonHasKey(json, "vehicleHandle")) {
            CVehicle* veh = HandleToVehicle((int)jsonGetNum(json, "vehicleHandle"));
            if (veh) plugin::scripting::CallCommandById((unsigned int)COMMAND_POINT_CAMERA_AT_CAR, veh, 2, 2);
        } else {
            float x = (float)jsonGetNum(json, "x");
            float y = (float)jsonGetNum(json, "y");
            float z = (float)jsonGetNum(json, "z");
            plugin::scripting::CallCommandById((unsigned int)COMMAND_POINT_CAMERA_AT_POINT, x, y, z, 2);
        }
        return "{}";
    }

    if (cmd == "camera_restore") {
        plugin::scripting::CallCommandById((unsigned int)COMMAND_RESTORE_CAMERA_JUMPCUT);
        return "{}";
    }

    if (cmd == "camera_fade") {
        float dur = (float)jsonGetNum(json, "duration", 1.0f);
        std::string dir = jsonGetStr(json, "direction", "in");
        short flag = (dir == "out") ? 0 : 1;
        TheCamera.Fade(dur, flag);
        return "{}";
    }

    if (cmd == "camera_shake") {
        float force = (float)jsonGetNum(json, "intensity", 1.0f);
        TheCamera.AddShakeSimple(2.0f, 0, force);
        return "{}";
    }

    if (cmd == "camera_set_mode") {
        short mode = (short)jsonGetNum(json, "mode", 4);
        TheCamera.m_nModeToGoTo = mode;
        return "{}";
    }

    // ==========================================
    // AUDIO & RADIO
    // ==========================================
    if (cmd == "audio_play_sound") {
        int soundId = (int)jsonGetNum(json, "soundId");
        float vol = (float)jsonGetNum(json, "volume", 1.0f);
        AudioEngine.ReportFrontendAudioEvent(soundId, vol, 1.0f);
        return "{}";
    }

    if (cmd == "audio_set_radio") {
        int station = (int)jsonGetNum(json, "stationId");
        AudioEngine.RetuneRadio((char)station);
        return "{\"stationId\":" + std::to_string(station) + "}";
    }

    // ==========================================
    // WORLD, OBJECTS, PICKUPS, FIRES
    // ==========================================
    if (cmd == "world_set_time") {
        int hour = (int)jsonGetNum(json, "hour");
        int minute = (int)jsonGetNum(json, "minute", 0);
        CClock::ms_nGameClockHours = (unsigned char)hour;
        CClock::ms_nGameClockMinutes = (unsigned char)minute;
        return "{}";
    }

    if (cmd == "world_get_time") {
        std::ostringstream o;
        o << "{\"hour\":" << (int)CClock::ms_nGameClockHours
          << ",\"minute\":" << (int)CClock::ms_nGameClockMinutes << "}";
        return o.str();
    }

    if (cmd == "world_set_weather") {
        int wid = (int)jsonGetNum(json, "weatherId");
        CWeather::ForceWeatherNow((short)wid);
        return "{}";
    }

    if (cmd == "world_set_timescale") {
        float scale = (float)jsonGetNum(json, "scale", 1.0);
        if (scale < 0.0f) scale = 0.0f;
        if (scale > 10.0f) scale = 10.0f;
        CTimer::ms_fTimeScale = scale;
        return "{\"applied\":" + fmtf(scale) + "}";
    }

    if (cmd == "world_set_gravity") {
        float g = (float)jsonGetNum(json, "gravity", 0.008f);
        *reinterpret_cast<float*>(0x863984) = g;
        return "{\"gravity\":" + fmtf(g) + "}";
    }

    if (cmd == "world_set_traffic_density") {
        float m = (float)jsonGetNum(json, "multiplier", 1.0f);
        *reinterpret_cast<float*>(0x8A5B20) = m;
        return "{\"multiplier\":" + fmtf(m) + "}";
    }

    if (cmd == "world_set_ped_density") {
        float m = (float)jsonGetNum(json, "multiplier", 1.0f);
        *reinterpret_cast<float*>(0x8D2530) = m;
        return "{\"multiplier\":" + fmtf(m) + "}";
    }

    if (cmd == "world_explosion") {
        CVector pos = { (float)jsonGetNum(json, "x"), (float)jsonGetNum(json, "y"), (float)jsonGetNum(json, "z") };
        int type = (int)jsonGetNum(json, "type", 0);
        CExplosion::AddExplosion(nullptr, nullptr, (eExplosionType)type, pos, 0, 1, 1.0f, 1);
        return "{}";
    }

    if (cmd == "world_create_object") {
        int modelId = (int)jsonGetNum(json, "modelId");
        float x = (float)jsonGetNum(json, "x");
        float y = (float)jsonGetNum(json, "y");
        float z = (float)jsonGetNum(json, "z");

        CStreaming::RequestModel(modelId, GAME_REQUIRED);
        CStreaming::LoadAllRequestedModels(false);
        if (!CStreaming::HasModelLoaded(modelId)) return "null";

        CObject* obj = CObject::Create(modelId);
        if (!obj) return "null";
        obj->SetPosition(x, y, z);
        obj->UpdateRwMatrix();
        CWorld::Add(obj);
        CStreaming::SetModelIsDeletable(modelId);

        int handle = ObjectToHandle(obj);
        return "{\"handle\":" + std::to_string(handle) + ",\"modelId\":" + std::to_string(modelId) + "}";
    }

    if (cmd == "world_remove_object") {
        int handle = (int)jsonGetNum(json, "handle");
        CObject* obj = HandleToObject(handle);
        if (!obj) return "null";
        CWorld::Remove(obj);
        delete obj;
        return "{}";
    }

    if (cmd == "world_create_pickup") {
        int modelId = (int)jsonGetNum(json, "modelId", 0);
        int weaponType = (int)jsonGetNum(json, "weaponType", 0);
        int ammo = (int)jsonGetNum(json, "ammo", 100);
        float x = (float)jsonGetNum(json, "x");
        float y = (float)jsonGetNum(json, "y");
        float z = (float)jsonGetNum(json, "z");
        int pType = (int)jsonGetNum(json, "pickupType", 3);

        int pHandle = -1;
        if (weaponType > 0) {
            pHandle = CPickups::GenerateNewOne_WeaponType({ x, y, z }, (eWeaponType)weaponType, (unsigned char)pType, ammo, false, nullptr);
        } else {
            CStreaming::RequestModel(modelId, GAME_REQUIRED);
            CStreaming::LoadAllRequestedModels(false);
            pHandle = CPickups::GenerateNewOne({ x, y, z }, modelId, (unsigned char)pType, ammo, 0, false, nullptr);
            CStreaming::SetModelIsDeletable(modelId);
        }
        return "{\"handle\":" + std::to_string(pHandle) + "}";
    }

    if (cmd == "world_remove_pickup") {
        int handle = (int)jsonGetNum(json, "handle");
        CPickups::RemovePickUp(handle);
        return "{}";
    }

    if (cmd == "world_create_fire") {
        float x = (float)jsonGetNum(json, "x");
        float y = (float)jsonGetNum(json, "y");
        float z = (float)jsonGetNum(json, "z");
        float size = (float)jsonGetNum(json, "size", 1.0f);
        gFireManager.StartFire({ x, y, z }, size, 0, nullptr, 10000, 1, 0);
        return "{}";
    }

    if (cmd == "world_extinguish_fires") {
        float x = (float)jsonGetNum(json, "x");
        float y = (float)jsonGetNum(json, "y");
        float z = (float)jsonGetNum(json, "z");
        float radius = (float)jsonGetNum(json, "radius", 25.0f);
        gFireManager.ExtinguishPoint({ x, y, z }, radius);
        return "{}";
    }

    if (cmd == "radar_blips") {
        if (!CRadar::ms_RadarTrace) return "null";
        std::ostringstream o;
        o << "{\"blips\":[";
        bool first = true;
        for (unsigned int i = 0; i < MAX_RADAR_TRACES; i++) {
            const tRadarTrace& b = CRadar::ms_RadarTrace[i];
            if (!b.m_bInUse) continue;
            if (!first) o << ",";
            o << "{\"index\":" << i
              << ",\"type\":" << (int)b.m_nBlipType
              << ",\"x\":" << fmtf(b.m_vecPos.x)
              << ",\"y\":" << fmtf(b.m_vecPos.y)
              << ",\"z\":" << fmtf(b.m_vecPos.z)
              << ",\"colour\":" << b.m_nColour
              << ",\"sprite\":" << (int)b.m_nRadarSprite
              << ",\"size\":" << (int)b.m_nBlipSize
              << ",\"shortRange\":" << (b.m_bShortRange ? "true" : "false")
              << ",\"entityHandle\":" << b.m_nEntityHandle
              << "}";
            first = false;
        }
        o << "]}";
        return o.str();
    }

    if (cmd == "radar_add_blip") {
        float x = (float)jsonGetNum(json, "x");
        float y = (float)jsonGetNum(json, "y");
        float z = (float)jsonGetNum(json, "z");
        int color = (int)jsonGetNum(json, "color", 2);
        int sprite = (int)jsonGetNum(json, "sprite", 0);
        int blip = CRadar::SetCoordBlip(BLIP_COORD, { x, y, z }, color, BLIP_DISPLAY_BOTH, nullptr);
        if (sprite > 0) CRadar::SetBlipSprite(blip, sprite);
        return "{\"blipIndex\":" + std::to_string(blip) + "}";
    }

    if (cmd == "radar_remove_blip") {
        int blip = (int)jsonGetNum(json, "blipIndex");
        CRadar::ClearBlip(blip);
        return "{}";
    }

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

    // ==========================================
    // CHEATS
    // ==========================================
    if (cmd == "game_cheat") {
        std::string name = jsonGetStr(json, "cheat");
        if (name == "health_armour_money") {
            CCheat::MoneyArmourHealthCheat();
        } else if (name == "weapon1") {
            CCheat::m_aCheatFunctions[CHEAT_WEAPON_SET1]();
        } else if (name == "weapon2") {
            CCheat::m_aCheatFunctions[CHEAT_WEAPON_SET2]();
        } else if (name == "weapon3") {
            CCheat::m_aCheatFunctions[CHEAT_WEAPON_SET3]();
        } else if (name == "jetpack") {
            CCheat::JetpackCheat();
        } else if (name == "parachute") {
            CCheat::ParachuteCheat();
        } else if (name == "never_wanted") {
            CCheat::NotWantedCheat();
        } else if (name == "six_stars") {
            CCheat::m_aCheatFunctions[CHEAT_SIX_WANTED_STARS]();
        } else if (name == "clear_wanted") {
            CCheat::m_aCheatFunctions[CHEAT_CLEAR_WANTED_LEVEL]();
        } else if (name == "infinite_ammo") {
            CCheat::m_aCheatFunctions[CHEAT_INFINITE_AMMO]();
        } else if (name == "infinite_health") {
            CCheat::m_aCheatFunctions[CHEAT_INFINITE_HEALTH]();
        } else if (name == "blow_up_cars") {
            CCheat::BlowUpCarsCheat();
        } else if (name == "cars_fly") {
            CCheat::m_aCheatFunctions[CHEAT_CARS_FLY]();
        } else if (name == "cars_on_water") {
            CCheat::m_aCheatFunctions[CHEAT_CARS_ON_WATER]();
        } else if (name == "max_stamina") {
            CCheat::m_aCheatFunctions[CHEAT_MAX_STAMINA]();
        } else if (name == "max_muscle") {
            CCheat::MuscleCheat();
        } else if (name == "max_driving") {
            CCheat::m_aCheatFunctions[CHEAT_MAX_DRIVING_SKILLS]();
        } else if (name == "hitman_weapons") {
            CCheat::m_aCheatFunctions[CHEAT_HITMAN_LEVEL_FOR_ALL_WEAPONS]();
        } else {
            return "{\"error\":\"unknown cheat name\"}";
        }
        return "{\"cheat\":" + jsonStr(name) + ",\"applied\":true}";
    }

    // ==========================================
    // SCRIPT & SCM COMMAND EXECUTION
    // ==========================================
    if (cmd == "script_call_opcode") {
        int opcode = (int)jsonGetNum(json, "opcode");
        std::string argTypes = jsonGetStr(json, "types");
        std::string argValues = jsonGetStr(json, "values");
        
        DynScriptCode code((short)opcode);
        std::stringstream ssTypes(argTypes);
        std::stringstream ssVals(argValues);
        std::string t, v;

        while (std::getline(ssTypes, t, ',') && std::getline(ssVals, v, ',')) {
            if (t == "int") {
                code.AddInt((int)strtol(v.c_str(), nullptr, 10));
            } else if (t == "float") {
                code.AddFloat((float)strtod(v.c_str(), nullptr));
            } else if (t == "str") {
                code.AddString(v.c_str());
            } else if (t == "ped") {
                CPed* p = HandleToPed((int)strtol(v.c_str(), nullptr, 10));
                code.AddPed(p);
            } else if (t == "veh") {
                CVehicle* veh = HandleToVehicle((int)strtol(v.c_str(), nullptr, 10));
                code.AddVehicle(veh);
            } else if (t == "obj") {
                CObject* obj = HandleToObject((int)strtol(v.c_str(), nullptr, 10));
                code.AddObject(obj);
            }
        }

        static CRunningScript script;
        memset(&script, 0, sizeof(CRunningScript));
        script.Init();
        strcpy_s(script.m_szName, "mcp-dyn");
        script.m_bIsMission = false;
        script.m_bUseMissionCleanup = false;
        script.m_bNotFlag = (opcode >> 15) & 1;
        script.m_pBaseIP = script.m_pCurrentIP = code.data;
        script.ProcessOneCommand();
        bool cond = script.m_bCondResult ? true : false;
        return "{\"opcode\":" + std::to_string(opcode) + ",\"result\":" + (cond ? "true" : "false") + "}";
    }

    // ==========================================
    // MEMORY & KEYS
    // ==========================================
    if (cmd == "key_tap") {
        int vk = (int)jsonGetNum(json, "keyCode");
        int frames = (int)jsonGetNum(json, "frames", 2);
        if (frames < 1) frames = 1;
        if (frames > 60) frames = 60;
        if (vk <= 0 || vk > 255) return "{\"error\":\"keyCode must be 1-255 (Windows VK_*)\"}";
        keybd_event((BYTE)vk, 0, 0, 0);
        g_pendingKeys.push_back({ vk, frames });
        return "{}";
    }

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

    if (cmd == "memory_nop") {
        uintptr_t addr = (uintptr_t)jsonGetNum(json, "address");
        size_t size = (size_t)jsonGetNum(json, "size", 1);
        plugin::patch::Nop(addr, size, true);
        return "{}";
    }

    if (cmd == "plugin_load") {
        std::string path = jsonGetStr(json, "path");
        if (path.empty()) return "{\"error\":\"path required\"}";

        BridgeLog("plugin_load: loading %s", path.c_str());
        HMODULE hMod = LoadLibraryA(path.c_str());
        if (!hMod) {
            DWORD err = GetLastError();
            BridgeLog("plugin_load: LoadLibrary failed err=%lu", err);
            return "{\"error\":\"LoadLibrary failed with error " + std::to_string(err) + "\"}";
        }
        BridgeLog("plugin_load: loaded successfully %s at %p", path.c_str(), hMod);
        return "{\"loaded\":true,\"handle\":" + std::to_string(reinterpret_cast<uintptr_t>(hMod)) + "}";
    }

    if (cmd == "plugin_unload") {
        std::string name = jsonGetStr(json, "pluginName");
        if (name.empty()) return "{\"error\":\"pluginName required\"}";
        if (name.find("debug_bridge") != std::string::npos)
            return "{\"error\":\"cannot unload debug_bridge.asi itself\"}";

        HMODULE hMod = GetModuleHandleA(name.c_str());
        if (!hMod) return "{\"error\":\"module not loaded\"}";

        BridgeLog("plugin_unload: unloading %s at %p", name.c_str(), hMod);
        using UnloadFn = void(*)();
        UnloadFn fn = reinterpret_cast<UnloadFn>(GetProcAddress(hMod, "GtaMcpUnload"));
        if (!fn) fn = reinterpret_cast<UnloadFn>(GetProcAddress(hMod, "PluginUnload"));
        if (!fn) fn = reinterpret_cast<UnloadFn>(GetProcAddress(hMod, "OnUnload"));
        if (fn) {
            BridgeLog("plugin_unload: calling exported unload function");
            fn();
        }

        FreeLibrary(hMod);
        return "{\"unloaded\":true}";
    }

    if (cmd == "plugin_reload") {
        std::string name = jsonGetStr(json, "pluginName");
        std::string path = jsonGetStr(json, "path", name);
        if (name.empty() && path.empty()) return "{\"error\":\"pluginName or path required\"}";
        if (name.empty()) {
            size_t slash = path.find_last_of("\\/");
            name = (slash != std::string::npos) ? path.substr(slash + 1) : path;
        }
        if (name.find("debug_bridge") != std::string::npos)
            return "{\"error\":\"cannot reload debug_bridge.asi itself — restart game to update bridge\"}";

        HMODULE hMod = GetModuleHandleA(name.c_str());
        if (hMod) {
            BridgeLog("plugin_reload: unloading previous instance of %s at %p", name.c_str(), hMod);
            using UnloadFn = void(*)();
            UnloadFn fn = reinterpret_cast<UnloadFn>(GetProcAddress(hMod, "GtaMcpUnload"));
            if (!fn) fn = reinterpret_cast<UnloadFn>(GetProcAddress(hMod, "PluginUnload"));
            if (!fn) fn = reinterpret_cast<UnloadFn>(GetProcAddress(hMod, "OnUnload"));
            if (fn) {
                BridgeLog("plugin_reload: calling exported unload function");
                fn();
            }
            FreeLibrary(hMod);
        }

        BridgeLog("plugin_reload: loading %s", path.c_str());
        HMODULE hNewMod = LoadLibraryA(path.c_str());
        if (!hNewMod) {
            DWORD err = GetLastError();
            BridgeLog("plugin_reload: LoadLibrary failed err=%lu", err);
            return "{\"error\":\"LoadLibrary failed with error " + std::to_string(err) + "\"}";
        }
        BridgeLog("plugin_reload: reloaded successfully %s at %p", path.c_str(), hNewMod);
        return "{\"reloaded\":true,\"handle\":" + std::to_string(reinterpret_cast<uintptr_t>(hNewMod)) + "}";
    }

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
                break;

            {
                std::lock_guard<std::mutex> lk(g_mutex);
                while (!g_respQueue.empty()) {
                    std::string line = g_respQueue.front().line + "\n";
                    g_respQueue.pop_front();
                    DWORD w;
                    WriteFile(g_pipe, line.c_str(), (DWORD)line.size(), &w, nullptr);
                }
            }

            if (avail == 0) { Sleep(8); continue; }

            DWORD read = 0;
            DWORD toRead = std::min(avail, (DWORD)sizeof(chunk));
            if (!ReadFile(g_pipe, chunk, toRead, &read, nullptr) || read == 0) break;

            buf.append(chunk, read);

            size_t pos;
            while ((pos = buf.find('\n')) != std::string::npos) {
                std::string line = buf.substr(0, pos);
                buf.erase(0, pos + 1);
                if (line.empty()) continue;

                int id = (int)jsonGetNum(line, "id");
                std::string cmd = jsonGetStr(line, "cmd");

                bool menuActive = FrontEndMenuManager.m_bMenuActive;
                bool playerReady = CWorld::Players[0].m_pPed != nullptr;
                bool cutsceneActive = playerReady &&
                    (CCutsceneMgr::ms_running || CCutsceneMgr::ms_cutsceneProcessing);
                const char* scene = menuActive ? (playerReady ? "paused" : "menu")
                                   : !playerReady ? "loading"
                                   : cutsceneActive ? "cutscene"
                                   : "in_game";

                if (cmd == "game_state") {
                    std::ostringstream o;
                    o << "{\"id\":" << id << ",\"ok\":true,\"data\":{\"scene\":\"" << scene
                      << "\",\"menuVisible\":" << (menuActive ? "true" : "false")
                      << ",\"playerReady\":" << (playerReady ? "true" : "false")
                      << ",\"cutsceneActive\":" << (cutsceneActive ? "true" : "false") << "}}";
                    std::lock_guard<std::mutex> lk(g_mutex);
                    g_respQueue.push_back({ o.str() });
                } else {
                    std::lock_guard<std::mutex> lk(g_mutex);
                    g_cmdQueue.push_back({ id, cmd, line, GetTickCount64() });
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
        {
            injector::game_version_manager gvm;
            gvm.PluginName = "debug_bridge";
            if (!gvm.Detect() || !gvm.IsSA() || !gvm.IsUS() ||
                gvm.GetMajorVersion() != 1 || gvm.GetMinorVersion() != 0) {
                gvm.RaiseIncompatibleVersion();
                return;
            }
        }

        char gameDir[MAX_PATH];
        GetModuleFileNameA(nullptr, gameDir, MAX_PATH);
        std::string dir(gameDir);
        dir = dir.substr(0, dir.rfind('\\'));
        std::string sentinel = dir + "\\mcp_debug.lock";

        if (GetFileAttributesA(sentinel.c_str()) == INVALID_FILE_ATTRIBUTES)
            return;

        g_running = true;
        g_fastboot = true;
        std::thread(pipeThread).detach();
        BridgeLog("boot: debug_bridge ctor, debug mode on");

        Events::menuDrawingEvent += [] {
            if (!g_fastboot) return;
            static bool s_frontendLogged = false;
            if (!s_frontendLogged) {
                s_frontendLogged = true;
                BridgeLog("boot: frontend draw started");
            }
            bool playerReady = CWorld::Players[0].m_pPed != nullptr;
            if (!playerReady && ++g_fastbootTimer >= 20) {
                g_fastbootTimer = 0;
                BridgeLog("boot: triggering new game (20 frontend frames)");
                FrontEndMenuManager.DoSettingsBeforeStartingAGame();
            }
        };

        Events::gameProcessEvent += [] {
            if (g_fastboot) {
                bool menuActive = FrontEndMenuManager.m_bMenuActive;
                bool playerReady = CWorld::Players[0].m_pPed != nullptr;
                bool cutsceneActive = CCutsceneMgr::ms_running || CCutsceneMgr::ms_cutsceneProcessing;

                if (!menuActive && playerReady && cutsceneActive) {
                    static bool s_cutsceneLogged = false;
                    if (!s_cutsceneLogged) {
                        s_cutsceneLogged = true;
                        BridgeLog("boot: intro cutscene detected, skipping");
                    }
                    if (++g_fastbootTimer >= 5) {
                        g_fastbootTimer = 0;
                        CCutsceneMgr::ms_wasCutsceneSkipped = true;
                    }
                } else if (!menuActive && playerReady && !cutsceneActive) {
                    g_fastboot = false;
                    BridgeLog("boot: gameplay reached");
                }
            }

            for (auto it = g_pendingKeys.begin(); it != g_pendingKeys.end();) {
                if (--it->framesLeft <= 0) {
                    keybd_event((BYTE)it->vk, 0, KEYEVENTF_KEYUP, 0);
                    it = g_pendingKeys.erase(it);
                } else {
                    ++it;
                }
            }

            std::lock_guard<std::mutex> lk(g_mutex);

            ULONGLONG nowMs = GetTickCount64();
            while (!g_cmdQueue.empty() && nowMs - g_cmdQueue.front().receivedAtMs > kQueueTtlMs) {
                BridgeCommand c = g_cmdQueue.front();
                g_cmdQueue.pop_front();
                g_respQueue.push_back({ "{\"id\":" + std::to_string(c.id) +
                    ",\"ok\":false,\"error\":\"queued command expired waiting for gameplay (15s)\"}" });
            }

            bool playerReady = CWorld::Players[0].m_pPed != nullptr;
            bool cutsceneActive = CCutsceneMgr::ms_running || CCutsceneMgr::ms_cutsceneProcessing;
            if (!playerReady || cutsceneActive || FrontEndMenuManager.m_bMenuActive)
                return;

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
