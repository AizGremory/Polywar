#include <list>
#include <vector>
#include <string.h>
#include <pthread.h>
#include <thread>
#include <cstring>
#include <jni.h>
#include <unistd.h>
#include <fstream>
#include <iostream>
#include <unordered_map>
#include <unordered_set>
#include <iomanip>
#include <cctype>
#include <sstream>
#include <array>
#include <atomic>
#include <dlfcn.h>
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include "Includes/Logger.h"
#include "Includes/obfuscate.h"
#include "imgui/FONTS/DEFAULT.h"
#include "imgui/Font.h"
#include "imgui/Icon.h"
#include "imgui/Iconcpp.h"
#include "imgui/imgui.h"
#include "imgui/imgui_additional.h"
#include "imgui/backends/imgui_impl_android.h"
#include "imgui/backends/imgui_impl_opengl3.h"
#include "imgui/android_native_app_glue.h"
#include "Includes/Vector2.h"
#include "Includes/Vector3.h"
#include "Includes/Utils.h"
#include "KittyMemory/MemoryPatch.h"
#include "Menu/Setup.h"
#include <sys/system_properties.h>
#include <Includes/Dobby/dobby.h>
#include "ByNameModding/Tools.h"
#include "ByNameModding/Il2Cpp.h"
#include "Il2Cpp/il2cpp_dump.h"
#include "Includes/log.h"
#include "Roboto-Regular.h"

// Target: Polywar / KUBOOM
#define targetLibName OBFUSCATE("libil2cpp.so")
#define GAME_PACKAGE "com.Nobodyshot.kuboom"

#include "JNIStuff.h"
#include "Includes/Macros.h"
#include "MonoString.h"
#include "input.h"

int glWidth, glHeight;
bool g_Initialized;
uintptr_t g_IL2Cpp;
void *m_EGL;
std::atomic<bool> g_Il2CppReady(false);

std::string GetProp(const char* key) {
    char value[PROP_VALUE_MAX];
    __system_property_get(key, value);
    return std::string(value);
}

// ============================================================
// POLYWAR CHEATS - REAL HOOKS FROM DUMP
// ============================================================

// --- Skin Changer ---
bool bSkinChanger = false;
int skinID = 1;
int (*orig_GameParamsScript_get_playerSkin)(void *instance);
int hook_GameParamsScript_get_playerSkin(void *instance) {
    if (instance != NULL) {
        if (bSkinChanger) return skinID;
    }
    return orig_GameParamsScript_get_playerSkin(instance);
}

// --- Damage Multiplier ---
bool bDamageMultiplier = false;
float damageMultiplier = 1.0f;
float (*orig_Weapon_get_weaponDamage)(void *instance);
float hook_Weapon_get_weaponDamage(void *instance) {
    if (instance != NULL) {
        if (bDamageMultiplier) return orig_Weapon_get_weaponDamage(instance) * damageMultiplier;
    }
    return orig_Weapon_get_weaponDamage(instance);
}

// --- God Mode ---
bool bGodMode = false;
void (*orig_SoldierController_TakeDamage)(void *instance, float amount);
void hook_SoldierController_TakeDamage(void *instance, float amount) {
    if (instance != NULL) {
        if (bGodMode) return;
    }
    orig_SoldierController_TakeDamage(instance, amount);
}

// --- Unlimited Ammo ---
bool bUnlimitedAmmo = false;
int (*orig_Weapon_get_magazine)(void *instance);
int hook_Weapon_get_magazine(void *instance) {
    if (instance != NULL) {
        if (bUnlimitedAmmo) return 999;
    }
    return orig_Weapon_get_magazine(instance);
}

// --- Visual Toggles ---
// These are used by the player-cache hook below, so keep their definitions
// before the first call to bESPEnabled().
bool bWallhack = false;
bool bChams = false;
bool bESP = false;
bool bESPBox = false;
bool bESPLine = false;
bool bESPHealth = false;
bool bESPDistance = false;
float espLineWidth = 3.4f;

// ============================================================
// ESP PLAYER CACHE
// ============================================================
struct PlayerCache {
    void *playerScript;
    int actorID;
    int team;
    bool isLocal;
    bool isDead;
    float health;
    float maxHealth;
};
PlayerCache g_PlayerCache[32];
int g_PlayerCacheCount = 0;
int g_LocalTeam = -1;
void *g_LocalPlayerScript = nullptr;

// --- Player Cache Hook ---
bool bESPEnabled() { return bESPBox || bESPLine || bESP || bESPHealth || bESPDistance; }
void (*orig_PlayerScript_SharedUpdate)(void *instance);
void hook_PlayerScript_SharedUpdate(void *instance) {
    orig_PlayerScript_SharedUpdate(instance);
    if (instance && bESPEnabled()) {
        // PlayerScript -> Pawn -> Photon.MonoBehaviour -> pvCache at 0x20
        void *photonView = *(void **)((uintptr_t)instance + 0x20); // pvCache
        if (photonView) {
            int viewID = *(int *)((uintptr_t)photonView + 0x70); // viewIdField
            int ownerId = *(int *)((uintptr_t)photonView + 0x28); // ownerId
            if (viewID > 0) {
                bool found = false;
                for (int i = 0; i < g_PlayerCacheCount; i++) {
                    if (g_PlayerCache[i].playerScript == instance) {
                        g_PlayerCache[i].actorID = ownerId;
                        g_PlayerCache[i].isDead = *(bool *)((uintptr_t)instance + 0x30); // Pawn.dead
                        found = true;
                        break;
                    }
                }
                if (!found && g_PlayerCacheCount < 32) {
                    g_PlayerCache[g_PlayerCacheCount].playerScript = instance;
                    g_PlayerCache[g_PlayerCacheCount].actorID = ownerId;
                    g_PlayerCache[g_PlayerCacheCount].isDead = false;
                    g_PlayerCache[g_PlayerCacheCount].team = 0;
                    g_PlayerCache[g_PlayerCacheCount].isLocal = false;
                    g_PlayerCache[g_PlayerCacheCount].health = 100;
                    g_PlayerCache[g_PlayerCacheCount].maxHealth = 100;
                    g_PlayerCacheCount++;
                }
            }
        }
    }
}

// --- Fire Rate Mod ---
bool bFireRateMod = false;
float fireRateMultiplier = 2.0f;
float (*orig_Weapon_get_fireRate)(void *instance);
float hook_Weapon_get_fireRate(void *instance) {
    if (instance != NULL) {
        if (bFireRateMod) return orig_Weapon_get_fireRate(instance) * fireRateMultiplier;
    }
    return orig_Weapon_get_fireRate(instance);
}

// --- Speed Hack ---
bool bSpeedHack = false;
float speedMultiplier = 2.0f;
float (*orig_PlayerController_get_walkSpeed)(void *instance);
float hook_PlayerController_get_walkSpeed(void *instance) {
    if (instance != NULL) {
        if (bSpeedHack) return orig_PlayerController_get_walkSpeed(instance) * speedMultiplier;
    }
    return orig_PlayerController_get_walkSpeed(instance);
}

float (*orig_PlayerController_get_runSpeed)(void *instance);
float hook_PlayerController_get_runSpeed(void *instance) {
    if (instance != NULL) {
        if (bSpeedHack) return orig_PlayerController_get_runSpeed(instance) * speedMultiplier;
    }
    return orig_PlayerController_get_runSpeed(instance);
}

// --- Jump Hack ---
bool bJumpHack = false;
float jumpMultiplier = 2.0f;
float (*orig_PlayerController_get_jumpForce)(void *instance);
float hook_PlayerController_get_jumpForce(void *instance) {
    if (instance != NULL) {
        if (bJumpHack) return orig_PlayerController_get_jumpForce(instance) * jumpMultiplier;
    }
    return orig_PlayerController_get_jumpForce(instance);
}

typedef void* (*il2cpp_runtime_invoke_t)(void*, void*, void**, void**);
il2cpp_runtime_invoke_t g_il2cpp_runtime_invoke_fn = nullptr;

// ============================================================
// IMGUI MENU
// ============================================================

void DrawESP() {
    if (!bESPEnabled() || !g_Il2CppReady.load())
        return;

    static void *il2cpp_handle = nullptr;
    if (!il2cpp_handle)
        il2cpp_handle = dlopen(targetLibName, RTLD_NOW);
    if (!il2cpp_handle) return;

    auto drawList = ImGui::GetForegroundDrawList();
    if (!drawList) return;

    // Lazy init runtime_invoke
    if (!g_il2cpp_runtime_invoke_fn) {
        g_il2cpp_runtime_invoke_fn = (il2cpp_runtime_invoke_t)dlsym(il2cpp_handle, "il2cpp_runtime_invoke");
        if (!g_il2cpp_runtime_invoke_fn) return;
    }

    static void *camMainMI = nullptr;
    static void *camW2SMI = nullptr;
    static void *transGetPosMI = nullptr;
    static void *compGetTransMI = nullptr;
    static void *pnwGetPlayerListMI = nullptr;
    static void *ppGetIDMI = nullptr;
    static void *ppGetNickMI = nullptr;
    static void *ppGetIsLocalMI = nullptr;
    static void *pawnGetTeamMI = nullptr;
    static void *psGetOwnerMI = nullptr;
    static void *ppGetIsMasterMI = nullptr;

    if (!camMainMI) {
        camMainMI = Il2CppGetMethodInfo("UnityEngine.CoreModule.dll", "UnityEngine", "Camera", "get_main", 0);
        camW2SMI = Il2CppGetMethodInfo("UnityEngine.CoreModule.dll", "UnityEngine", "Camera", "WorldToScreenPoint", 1);
        transGetPosMI = Il2CppGetMethodInfo("UnityEngine.CoreModule.dll", "UnityEngine", "Transform", "get_position", 0);
        compGetTransMI = Il2CppGetMethodInfo("UnityEngine.CoreModule.dll", "UnityEngine", "Component", "get_transform", 0);
        pnwGetPlayerListMI = Il2CppGetMethodInfo("Assembly-CSharp.dll", "", "PhotonNetwork", "get_playerList", 0);
        ppGetIDMI = Il2CppGetMethodInfo("Assembly-CSharp.dll", "", "PhotonPlayer", "get_ID", 0);
        ppGetNickMI = Il2CppGetMethodInfo("Assembly-CSharp.dll", "", "PhotonPlayer", "get_NickName", 0);
        ppGetIsLocalMI = Il2CppGetMethodInfo("Assembly-CSharp.dll", "", "PhotonPlayer", "get_IsLocal", 0);
        pawnGetTeamMI = Il2CppGetMethodInfo("Assembly-CSharp.dll", "", "kube.Pawn", "getTeam", 0);
        ppGetIsMasterMI = Il2CppGetMethodInfo("Assembly-CSharp.dll", "", "PhotonPlayer", "get_IsMasterClient", 0);
    }

    if (!camMainMI || !camW2SMI || !pnwGetPlayerListMI) return;

    // Get Camera.main
    void *exc = nullptr;
    void *camera = g_il2cpp_runtime_invoke_fn(camMainMI, nullptr, nullptr, &exc);
    if (!camera || exc) return;

    // Get player list
    exc = nullptr;
    void *playerList = g_il2cpp_runtime_invoke_fn(pnwGetPlayerListMI, nullptr, nullptr, &exc);
    if (!playerList || exc) return;

    int arrLen = *(int *)((uintptr_t)playerList + 0x18);
    if (arrLen <= 0 || arrLen > 100) return;

    for (int i = 0; i < arrLen; i++) {
        void *photonPlayer = *(void **)((uintptr_t)playerList + 0x20 + i * 8);
        if (!photonPlayer) continue;

        // Get player ID
        exc = nullptr;
        void *boxedId = g_il2cpp_runtime_invoke_fn(ppGetIDMI, photonPlayer, nullptr, &exc);
        if (!boxedId || exc) continue;
        int actorID = *(int *)((uintptr_t)boxedId + 0x10);

        // Check if local
        exc = nullptr;
        void *boxedLocal = g_il2cpp_runtime_invoke_fn(ppGetIsLocalMI, photonPlayer, nullptr, &exc);
        bool isLocal = boxedLocal && !exc && *(uint8_t *)((uintptr_t)boxedLocal + 0x10);

        // Skip local player
        if (isLocal) continue;

        // Get nickname
        exc = nullptr;
        void *nameStr = g_il2cpp_runtime_invoke_fn(ppGetNickMI, photonPlayer, nullptr, &exc);
        char displayName[64] = "Player";
        if (nameStr && !exc) {
            int nameLen = *(int *)((uintptr_t)nameStr + 0x10);
            uint16_t *chars = (uint16_t *)((uintptr_t)nameStr + 0x14);
            int j = 0;
            for (int k = 0; k < nameLen && k < 31; k++) {
                uint16_t c = chars[k];
                if (c < 128) displayName[j++] = (char)c;
            }
            displayName[j] = '\0';
        }

        // Find PlayerScript from cache
        void *playerScript = nullptr;
        int team = 0;
        for (int j = 0; j < g_PlayerCacheCount; j++) {
            if (g_PlayerCache[j].actorID == actorID && !g_PlayerCache[j].isDead) {
                playerScript = g_PlayerCache[j].playerScript;
                team = g_PlayerCache[j].team;
                break;
            }
        }

        if (!playerScript) continue;

        // Get transform
        if (!compGetTransMI || !transGetPosMI) continue;
        exc = nullptr;
        void *transform = g_il2cpp_runtime_invoke_fn(compGetTransMI, playerScript, nullptr, &exc);
        if (!transform || exc) continue;

        // Get position
        exc = nullptr;
        void *boxedPos = g_il2cpp_runtime_invoke_fn(transGetPosMI, transform, nullptr, &exc);
        if (!boxedPos || exc) continue;

        float wx = *(float *)((uintptr_t)boxedPos + 0x10);
        float wy = *(float *)((uintptr_t)boxedPos + 0x14);
        float wz = *(float *)((uintptr_t)boxedPos + 0x18);

        // WorldToScreenPoint
        void *w2sP[1] = { boxedPos };
        exc = nullptr;
        void *boxedFoot = g_il2cpp_runtime_invoke_fn(camW2SMI, camera, w2sP, &exc);
        if (!boxedFoot || exc) continue;

        float footX = *(float *)((uintptr_t)boxedFoot + 0x10);
        float footY = *(float *)((uintptr_t)boxedFoot + 0x14);
        float screenZ = *(float *)((uintptr_t)boxedFoot + 0x18);
        if (screenZ <= 0 || footX < 0 || footX > glWidth || footY < 0 || footY > glHeight) continue;

        // Get head position
        float headPos[3] = {wx, wy + 2.0f, wz};
        uint8_t headBoxed[0x1C] = {0};
        *(void**)(headBoxed) = *(void**)boxedPos;
        *(float*)(headBoxed + 0x10) = headPos[0];
        *(float*)(headBoxed + 0x14) = headPos[1];
        *(float*)(headBoxed + 0x18) = headPos[2];

        void *w2sP2[1] = { headBoxed };
        exc = nullptr;
        void *boxedHead = g_il2cpp_runtime_invoke_fn(camW2SMI, camera, w2sP2, &exc);
        if (!boxedHead || exc) continue;

        float headY = *(float *)((uintptr_t)boxedHead + 0x14);

        float boxH = footY - headY;
        float boxW = boxH * 0.45f;
        float boxX = footX - boxW * 0.5f;
        float boxY = headY;

        // Colors based on team
        ImU32 color;
        if (team == g_LocalTeam && g_LocalTeam >= 0)
            color = IM_COL32(50, 150, 255, 255); // teammate = blue
        else
            color = IM_COL32(255, 50, 50, 255);   // enemy = red

        // Box ESP
        if (bESPBox) {
            drawList->AddRect(ImVec2(boxX, boxY), ImVec2(boxX + boxW, boxY + boxH),
                             color, 0.0f, 0, espLineWidth);
            drawList->AddRect(ImVec2(boxX - 1, boxY - 1), ImVec2(boxX + boxW + 1, boxY + boxH + 1),
                             IM_COL32(0, 0, 0, 200), 0.0f, 0, 1.0f);
        }

        // Line ESP
        if (bESPLine) {
            drawList->AddLine(ImVec2(glWidth / 2.0f, (float)glHeight),
                             ImVec2(footX, footY), color, espLineWidth);
        }

        // Name ESP
        if (bESP) {
            drawList->AddText(ImVec2(boxX + boxW + 3, boxY),
                             IM_COL32(255, 255, 255, 255), displayName);
        }

        // Distance ESP
        if (bESPDistance) {
            char distText[16];
            snprintf(distText, sizeof(distText), "%.0fm", screenZ);
            drawList->AddText(ImVec2(boxX + boxW + 3, boxY + 14),
                             IM_COL32(200, 200, 200, 255), distText);
        }

        // Health bar
        if (bESPHealth) {
            float hpPct = 1.0f;
            // Try to get health from cache
            for (int j = 0; j < g_PlayerCacheCount; j++) {
                if (g_PlayerCache[j].playerScript == playerScript) {
                    if (g_PlayerCache[j].maxHealth > 0)
                        hpPct = g_PlayerCache[j].health / g_PlayerCache[j].maxHealth;
                    break;
                }
            }
            if (hpPct < 0) hpPct = 0;
            if (hpPct > 1) hpPct = 1;

            float barW = 4.0f;
            float barX = boxX - barW - 3;
            float barY = boxY;
            float barH = boxH;

            // Background + border
            drawList->AddRectFilled(ImVec2(barX - 1, barY - 1), ImVec2(barX + barW + 1, barY + barH + 1), IM_COL32(0,0,0,180));
            // Health fill
            float fillH = barH * hpPct;
            ImU32 hpColor = hpPct > 0.5f ? IM_COL32(50, 220, 50, 220) : (hpPct > 0.25f ? IM_COL32(220, 220, 50, 220) : IM_COL32(255, 50, 50, 220));
            drawList->AddRectFilled(ImVec2(barX, barY + barH - fillH), ImVec2(barX + barW, barY + barH), hpColor);
            drawList->AddRect(ImVec2(barX - 1, barY - 1), ImVec2(barX + barW + 1, barY + barH + 1), IM_COL32(0,0,0,255), 0, 0, 1.0f);
        }
    }
}

void DrawMenu() {
    const ImVec2 window_size = ImVec2(720, 600);
    ImGui::SetNextWindowSize(window_size, ImGuiCond_Once);
    ImGui::Begin(" POLYWAR CHEAT | KUBOOM v2.7", nullptr, ImGuiWindowFlags_NoBringToFrontOnFocus);

        static int activeTab = 0;
        float windowWidth = ImGui::GetContentRegionAvail().x;

        float spacing = 8.0f;
        int tabCount = 4;
        float buttonWidth = (windowWidth - spacing * (tabCount - 1)) / tabCount;
        float buttonHeight = 48.0f;

        auto TabButton = [&](const char* label, int id) {
            ImGuiStyle& style = ImGui::GetStyle();
            float oldRounding = style.FrameRounding;
            style.FrameRounding = 0.0f;
            ImVec2 size(buttonWidth, buttonHeight);
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
            bool pressed = ImGui::Button(label, size);
            ImGui::PopStyleVar();
            if (pressed) activeTab = id;
            style.FrameRounding = oldRounding;
        };

        for (int i = 0; i < tabCount; i++) {
            if (i > 0) ImGui::SameLine(0, spacing);
            if (i == 0) TabButton(" COMBAT ", 0);
            else if (i == 1) TabButton(" MOVEMENT ", 1);
            else if (i == 2) TabButton(" VISUALS ", 2);
            else if (i == 3) TabButton(" TOOLS ", 3);
        }

        ImGui::Separator();
        float spacingChild = 8.0f;

        // --- COMBAT TAB ---
        if (activeTab == 0) {
            float halfWidth = (windowWidth - spacingChild) / 2.0f;

            ImGui::BeginChild("combat_left", ImVec2(halfWidth, 0), true);
            ImGui::PushItemWidth(-1);

            ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), " WEAPON ");
            ImGui::Separator();
            ImGui::Checkbox("Damage Multiplier", &bDamageMultiplier);
            if (bDamageMultiplier) {
                ImGui::SliderFloat("DMG##dmg", &damageMultiplier, 1.0f, 100.0f, "%.0fx");
            }
            ImGui::Checkbox("Unlimited Ammo", &bUnlimitedAmmo);
            ImGui::Checkbox("Fire Rate Mod", &bFireRateMod);
            if (bFireRateMod) {
                ImGui::SliderFloat("Rate##rate", &fireRateMultiplier, 0.5f, 10.0f, "%.1fx");
            }

            ImGui::Spacing();
            ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), " PLAYER ");
            ImGui::Separator();
            ImGui::Checkbox("God Mode", &bGodMode);

            ImGui::PopItemWidth();
            ImGui::EndChild();

            ImGui::SameLine(0, spacingChild);

            ImGui::BeginChild("combat_right", ImVec2(0, 0), true);
            ImGui::PushItemWidth(-1);

            ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), " SKIN CHANGER ");
            ImGui::Separator();
            ImGui::Checkbox("Skin Changer", &bSkinChanger);
            if (bSkinChanger) {
                ImGui::SliderInt("Skin ID", &skinID, 1, 99);
            }

            ImGui::Spacing();
            ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), " Extras ");
            ImGui::Separator();
            ImGui::TextDisabled("More soon...");

            ImGui::PopItemWidth();
            ImGui::EndChild();
        }

        // --- MOVEMENT TAB ---
        if (activeTab == 1) {
            ImGui::BeginChild("movement", ImVec2(0, 0), true);
            ImGui::PushItemWidth(-1);

            ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f), " SPEED ");
            ImGui::Separator();
            ImGui::Checkbox("Speed Hack", &bSpeedHack);
            if (bSpeedHack) {
                ImGui::SliderFloat("Speed##spd", &speedMultiplier, 1.0f, 10.0f, "%.1fx");
            }

            ImGui::Spacing();
            ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f), " JUMP ");
            ImGui::Separator();
            ImGui::Checkbox("Super Jump", &bJumpHack);
            if (bJumpHack) {
                ImGui::SliderFloat("Jump##jmp", &jumpMultiplier, 1.0f, 20.0f, "%.1fx");
            }

            ImGui::PopItemWidth();
            ImGui::EndChild();
        }

        // --- VISUALS TAB ---
        if (activeTab == 2) {
            float halfWidth = (windowWidth - spacingChild) / 2.0f;

            ImGui::BeginChild("visuals_left", ImVec2(halfWidth, 0), true);
            ImGui::PushItemWidth(-1);

            ImGui::TextColored(ImVec4(0.3f, 0.3f, 1.0f, 1.0f), " ESP PLAYER ");
            ImGui::Separator();
            ImGui::Checkbox("Box ESP", &bESPBox);
            ImGui::Checkbox("Line ESP", &bESPLine);
            ImGui::Checkbox("Name ESP", &bESP);
            ImGui::Checkbox("Health Bar", &bESPHealth);
            ImGui::Checkbox("Distance", &bESPDistance);
            ImGui::SliderFloat("Line Width", &espLineWidth, 0.5f, 10.0f, "%.1f");

            ImGui::PopItemWidth();
            ImGui::EndChild();

            ImGui::SameLine(0, spacingChild);

            ImGui::BeginChild("visuals_right", ImVec2(0, 0), true);
            ImGui::PushItemWidth(-1);

            ImGui::TextColored(ImVec4(0.3f, 0.3f, 1.0f, 1.0f), " VISUAL FX ");
            ImGui::Separator();
            ImGui::Checkbox("Chams / Colored", &bChams);
            ImGui::Checkbox("Wallhack", &bWallhack);

            ImGui::PopItemWidth();
            ImGui::EndChild();
        }

        // --- TOOLS TAB ---
        if (activeTab == 3) {
            ImGui::BeginChild("tools", ImVec2(0, 0), true);
            ImGui::PushItemWidth(-1);

            ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.3f, 1.0f), " DEVICE INFO ");
            ImGui::Separator();
            ImGui::Bullet(); ImGui::SameLine();
            ImGui::Text("Device: %s", GetProp("ro.product.device").c_str());
            ImGui::Bullet(); ImGui::SameLine();
            ImGui::Text("Model: %s", GetProp("ro.product.model").c_str());
            ImGui::Bullet(); ImGui::SameLine();
            ImGui::Text("Manufacturer: %s", GetProp("ro.product.manufacturer").c_str());
            ImGui::Bullet(); ImGui::SameLine();
            ImGui::Text("Android: %s", GetProp("ro.build.version.release").c_str());
            ImGui::Bullet(); ImGui::SameLine();
            ImGui::Text("CPU: %s", GetProp("ro.product.cpu.abi").c_str());

            ImGui::Spacing();
            ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.3f, 1.0f), " DUMPER ");
            ImGui::Separator();
            ImGui::TextDisabled("Dump game scripts to:");
            ImGui::TextDisabled("sdcard/Android/data/com.Nobodyshot.kuboom/dump.cs");

            if (ImGui::Button("DUMP IL2CPP", ImVec2(ImGui::GetContentRegionAvail().x, 0))) {
                auto handle = dlopen(targetLibName, 4);
                if (handle) {
                    il2cpp_dump(handle);
                }
            }

            ImGui::PopItemWidth();
            ImGui::EndChild();
        }

    ImGui::End();
}

// ============================================================
// EGL & RENDER HOOK
// ============================================================

EGLBoolean (*old_eglSwapBuffers)(EGLDisplay dpy, EGLSurface surface);
EGLBoolean hook_eglSwapBuffers(EGLDisplay dpy, EGLSurface surface) {
    if (!old_eglSwapBuffers)
        return EGL_FALSE;

    EGLint width = 0;
    EGLint height = 0;
    if (!eglQuerySurface(dpy, surface, EGL_WIDTH, &width) ||
        !eglQuerySurface(dpy, surface, EGL_HEIGHT, &height) ||
        width <= 0 || height <= 0) {
        return old_eglSwapBuffers(dpy, surface);
    }

    glWidth = width;
    glHeight = height;

    pthread_mutex_lock(&g_ImGuiMutex);

    static bool is_setup = false;
    if (!is_setup) {
        ImGui::CreateContext();
        ImGuiIO &io = ImGui::GetIO();
        io.DisplaySize = ImVec2(glWidth, glHeight);
        ImGui::GetStyle().WindowTitleAlign = ImVec2(0.5, 0.5);
        ImGui::StyleColorsDark();
        ImGui_ImplOpenGL3_Init("#version 100");
        io.IniFilename = nullptr;

        ImFontConfig font_cfg;
        font_cfg.SizePixels = 35.0f;
        static const ImWchar ranges[] = {
            0x0020, 0x00FF, 0x0100, 0x024F, 0x0400, 0x052F, 0,
        };
        io.Fonts->AddFontFromMemoryTTF(Roboto_Regular, sizeof(Roboto_Regular), 35.0f, &font_cfg, ranges);
        is_setup = true;
        LOGI("ImGui initialized (%dx%d)", glWidth, glHeight);
    }

    ImGui_ImplAndroid_NewFrame(glWidth, glHeight);
    ImGui_ImplOpenGL3_NewFrame();
    ImGui::NewFrame();
    DrawMenu();
    DrawESP();
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    pthread_mutex_unlock(&g_ImGuiMutex);

    return old_eglSwapBuffers(dpy, surface);
}

static bool InstallEglHook() {
    void *egl_handle = dlopen("libEGL.so", RTLD_NOW);
    void *swap_buffers = egl_handle ? dlsym(egl_handle, "eglSwapBuffers") : nullptr;
    if (!swap_buffers)
        swap_buffers = DobbySymbolResolver("libEGL.so", OBFUSCATE("eglSwapBuffers"));

    if (!swap_buffers) {
        LOGE("EGL hook failed: eglSwapBuffers was not found");
        return false;
    }

    const int result = DobbyHook(swap_buffers, (void *)hook_eglSwapBuffers,
                                 (void **)&old_eglSwapBuffers);
    if (result != RT_SUCCESS || !old_eglSwapBuffers) {
        LOGE("EGL hook failed: Dobby returned %d", result);
        return false;
    }

    LOGI("EGL hook installed at %p", swap_buffers);
    return true;
}

// ============================================================
// MAIN HOOK THREAD
// ============================================================

void *hack_thread(void *) {
    LOGI(OBFUSCATE("Polywar cheat thread started"));

    InstallEglHook();
    __INPUT__();

    // Wait for il2cpp
    do { sleep(1); } while (!isLibraryLoaded(targetLibName));
    LOGI(OBFUSCATE("libil2cpp.so loaded, hooking..."));

    // Init IL2CPP
    if (!Il2CppAttach(targetLibName))
        return nullptr;
    g_Il2CppReady.store(true);
    sleep(3);

    // ============================================================
    // HOOKS FROM POLYWAR DUMP
    // ============================================================

    // Skin Changer: GameParamsScript.get_playerSkin()
    void *skinMethod = Il2CppGetMethodOffset(
        "Assembly-CSharp.dll", "", "GameParamsScript", "get_playerSkin", 0);
    if (skinMethod) {
        DobbyHook(skinMethod, (void *)hook_GameParamsScript_get_playerSkin,
                  (void **)&orig_GameParamsScript_get_playerSkin);
        LOGI("Hooked: GameParamsScript.get_playerSkin");
    }

    // Damage: Weapon.get_weaponDamage
    void *wpnDmg = Il2CppGetMethodOffset(
        "Assembly-CSharp.dll", "", "Weapon", "get_weaponDamage", 0);
    if (wpnDmg) {
        DobbyHook(wpnDmg, (void *)hook_Weapon_get_weaponDamage,
                  (void **)&orig_Weapon_get_weaponDamage);
        LOGI("Hooked: Weapon.get_weaponDamage");
    }

    // God Mode: SoldierController.TakeDamage(float)
    char *takeDamageArgs[] = { (char *)"System.Single" };
    void *takeDmg = Il2CppGetMethodOffset(
        "Assembly-CSharp.dll", "", "SoldierController", "TakeDamage", takeDamageArgs, 1);
    if (takeDmg) {
        DobbyHook(takeDmg, (void *)hook_SoldierController_TakeDamage,
                  (void **)&orig_SoldierController_TakeDamage);
        LOGI("Hooked: SoldierController.TakeDamage");
    }

    // Ammo: Weapon.get_magazine
    void *wpnMag = Il2CppGetMethodOffset(
        "Assembly-CSharp.dll", "", "Weapon", "get_magazine", 0);
    if (wpnMag) {
        DobbyHook(wpnMag, (void *)hook_Weapon_get_magazine,
                  (void **)&orig_Weapon_get_magazine);
        LOGI("Hooked: Weapon.get_magazine");
    }

    // Fire Rate: Weapon.get_fireRate
    void *wpnFire = Il2CppGetMethodOffset(
        "Assembly-CSharp.dll", "", "Weapon", "get_fireRate", 0);
    if (wpnFire) {
        DobbyHook(wpnFire, (void *)hook_Weapon_get_fireRate,
                  (void **)&orig_Weapon_get_fireRate);
        LOGI("Hooked: Weapon.get_fireRate");
    }

    // Speed: PlayerController.get_walkSpeed
    void *walkSpeed = Il2CppGetMethodOffset(
        "Assembly-CSharp.dll", "", "PlayerController", "get_walkSpeed", 0);
    if (walkSpeed) {
        DobbyHook(walkSpeed, (void *)hook_PlayerController_get_walkSpeed,
                  (void **)&orig_PlayerController_get_walkSpeed);
        LOGI("Hooked: PlayerController.get_walkSpeed");
    }

    // Run Speed: PlayerController.get_runSpeed
    void *runSpeed = Il2CppGetMethodOffset(
        "Assembly-CSharp.dll", "", "PlayerController", "get_runSpeed", 0);
    if (runSpeed) {
        DobbyHook(runSpeed, (void *)hook_PlayerController_get_runSpeed,
                  (void **)&orig_PlayerController_get_runSpeed);
        LOGI("Hooked: PlayerController.get_runSpeed");
    }

    // Jump: PlayerController.get_jumpForce
    void *jumpForce = Il2CppGetMethodOffset(
        "Assembly-CSharp.dll", "", "PlayerController", "get_jumpForce", 0);
    if (jumpForce) {
        DobbyHook(jumpForce, (void *)hook_PlayerController_get_jumpForce,
                  (void **)&orig_PlayerController_get_jumpForce);
        LOGI("Hooked: PlayerController.get_jumpForce");
    }

    LOGI("All hooks installed!");

    // PlayerScript cache hook for ESP
    void *sharedUpdate = Il2CppGetMethodOffset(
        "Assembly-CSharp.dll", "", "PlayerScript", "SharedUpdate", 0);
    if (sharedUpdate) {
        DobbyHook(sharedUpdate, (void *)hook_PlayerScript_SharedUpdate,
                  (void **)&orig_PlayerScript_SharedUpdate);
        LOGI("Hooked: PlayerScript.SharedUpdate");
    }

    return NULL;
}

// ============================================================
// JAVA JNI BRIDGE
// ============================================================

jobjectArray EnglishList(JNIEnv *env, jobject context) {
    const char *features[] = {
        OBFUSCATE("Polywar Cheat v2.7 | KUBOOM | github.com/AizGremory"),
    };
    int total = sizeof(features) / sizeof(features[0]);
    jobjectArray ret = env->NewObjectArray(total, env->FindClass(OBFUSCATE("java/lang/String")),
                                           env->NewStringUTF(""));
    for (int i = 0; i < total; i++)
        env->SetObjectArrayElement(ret, i, env->NewStringUTF(features[i]));
    return ret;
}

void Changes(JNIEnv *env, jclass clazz, jobject obj,
             jint featNum, jstring featName, jint value,
             jboolean boolean, jstring str) {
    LOGD(OBFUSCATE("Feature: %d - %s | Value: %d | Bool: %d"), featNum,
         env->GetStringUTFChars(featName, 0), value, boolean);
    switch (featNum) {
        case 0: break;
    }
}

__attribute__((constructor))
void lib_main() {
    pthread_t ptid;
    pthread_create(&ptid, NULL, hack_thread, NULL);
}

int RegisterMenu(JNIEnv *env) {
    JNINativeMethod methods[] = {
        {OBFUSCATE("Icon"), OBFUSCATE("()Ljava/lang/String;"), reinterpret_cast<void *>(Icon)},
        {OBFUSCATE("IconWebViewData"), OBFUSCATE("()Ljava/lang/String;"), reinterpret_cast<void *>(IconWebViewData)},
        {OBFUSCATE("IsGameLibLoaded"), OBFUSCATE("()Z"), reinterpret_cast<void *>(isGameLibLoaded)},
        {OBFUSCATE("Init"), OBFUSCATE("(Landroid/content/Context;Landroid/widget/TextView;Landroid/widget/TextView;)V"), reinterpret_cast<void *>(Init)},
        {OBFUSCATE("SettingsList"), OBFUSCATE("()[Ljava/lang/String;"), reinterpret_cast<void *>(SettingsList)},
        {OBFUSCATE("EnglishList"), OBFUSCATE("()[Ljava/lang/String;"), reinterpret_cast<void *>(EnglishList)},
    };
    jclass clazz = env->FindClass(OBFUSCATE("com/polywar/cheat/Menu"));
    if (!clazz) {
        if (env->ExceptionCheck()) env->ExceptionClear();
        return JNI_ERR;
    }
    const int result = env->RegisterNatives(clazz, methods, sizeof(methods) / sizeof(methods[0]));
    env->DeleteLocalRef(clazz);
    if (result != 0) {
        if (env->ExceptionCheck()) env->ExceptionClear();
        return JNI_ERR;
    }
    return JNI_OK;
}

int RegisterPreferences(JNIEnv *env) {
    JNINativeMethod methods[] = {
        {OBFUSCATE("Changes"), OBFUSCATE("(Landroid/content/Context;ILjava/lang/String;IZLjava/lang/String;)V"), reinterpret_cast<void *>(Changes)},
    };
    jclass clazz = env->FindClass(OBFUSCATE("com/polywar/cheat/Preferences"));
    if (!clazz) {
        if (env->ExceptionCheck()) env->ExceptionClear();
        return JNI_ERR;
    }
    const int result = env->RegisterNatives(clazz, methods, sizeof(methods) / sizeof(methods[0]));
    env->DeleteLocalRef(clazz);
    if (result != 0) {
        if (env->ExceptionCheck()) env->ExceptionClear();
        return JNI_ERR;
    }
    return JNI_OK;
}

int RegisterMain(JNIEnv *env) {
    JNINativeMethod methods[] = {
        {OBFUSCATE("CheckOverlayPermission"), OBFUSCATE("(Landroid/content/Context;)V"), reinterpret_cast<void *>(CheckOverlayPermission)},
    };
    jclass clazz = env->FindClass(OBFUSCATE("com/polywar/cheat/Main"));
    if (!clazz) {
        if (env->ExceptionCheck()) env->ExceptionClear();
        return JNI_ERR;
    }
    const int result = env->RegisterNatives(clazz, methods, sizeof(methods) / sizeof(methods[0]));
    env->DeleteLocalRef(clazz);
    if (result != 0) {
        if (env->ExceptionCheck()) env->ExceptionClear();
        return JNI_ERR;
    }
    return JNI_OK;
}

extern "C"
JNIEXPORT jint JNICALL
JNI_OnLoad(JavaVM *vm, void *reserved) {
    jvm = vm;
    JNIEnv *env = nullptr;
    if (!vm || vm->GetEnv((void **)&env, JNI_VERSION_1_6) != JNI_OK || !env)
        return JNI_ERR;

    // These bridge classes exist in the standalone loader APK, but not when
    // the same .so is injected into the game process. Missing bridge classes
    // must not make ART unload the injected library while its worker is alive.
    int registered = 0;
    if (RegisterMenu(env) == JNI_OK) registered++;
    if (RegisterPreferences(env) == JNI_OK) registered++;
    if (RegisterMain(env) == JNI_OK) registered++;
    LOGI("JNI_OnLoad complete: %d bridge classes registered", registered);
    return JNI_VERSION_1_6;
}
