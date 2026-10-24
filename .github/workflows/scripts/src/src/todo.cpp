// =====================================================================
//  PHANTOM FF - TODO EL CODIGO EN UN ARCHIVO
//  Lo unico que se toca si algo no engancha es el bloque CONFIG.
// =====================================================================

#include <jni.h>
#include <pthread.h>
#include <dlfcn.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <android/input.h>
#include <android/log.h>

#include "imgui.h"
#include "imgui_impl_opengl3.h"
#include "dobby.h"

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,"PHANTOM",__VA_ARGS__)
#define PI 3.14159265f

// ============================ CONFIG ============================
namespace CFG {
    inline const char *PLAYER_CLASS = "Player";
    inline const char *LOCAL_FIELD  = "LocalPlayer";
    inline float       HEAD_OFFSET  = 1.55f;

    inline const char *HS_CLASS  = "Player";  inline const char *HS_METHOD  = "GetHitPart"; inline int HS_ARGC = 0;
    inline const char *AIM_CLASS = "Player";  inline const char *AIM_METHOD = "LateUpdate"; inline int AIM_ARGC = 0;
    inline const char *REC_CLASS = "Weapon";  inline const char *REC_METHOD = "GetRecoil";  inline int REC_ARGC = 0;
}

// ============================ ESTADO ============================
struct Vec3 { float x, y, z; };

static bool  g_MenuOpen = true;
static bool  hs_on = false, aim_on = false, rec_on = false;
static bool  ok_hs = false, ok_aim = false, ok_rec = false;
static float g_fov = 90.f, g_smooth = 0.35f;
static void *localPlayer = nullptr;
static void *o_hs = nullptr, *o_aim = nullptr, *o_rec = nullptr;

// ============================ IL2CPP ============================
typedef void  *(*t_domain_get)();
typedef void  *(*t_domain_assembly_open)(void *, const char *);
typedef void  *(*t_assembly_get_image)(void *);
typedef void  *(*t_class_from_name)(void *, const char *, const char *);
typedef void  *(*t_class_get_field_from_name)(void *, const char *);
typedef void   (*t_field_static_get_value)(void *, void *);
typedef void  *(*t_class_get_method_from_name)(void *, const char *, int);

static void *ih = nullptr;
static t_domain_get p_domain_get = nullptr;
static t_domain_assembly_open p_dom_asm = nullptr;
static t_assembly_get_image p_asm_img = nullptr;
static t_class_from_name p_class_from_name = nullptr;
static t_class_get_field_from_name p_field_from_name = nullptr;
static t_field_static_get_value p_field_static = nullptr;
static t_class_get_method_from_name p_method_from_name = nullptr;

static bool il2cpp_init() {
    ih = dlopen("libil2cpp.so", RTLD_NOW);
    if (!ih) { LOGI("no hay libil2cpp"); return false; }
    p_domain_get       = (t_domain_get) dlsym(ih, "il2cpp_domain_get");
    p_dom_asm          = (t_domain_assembly_open) dlsym(ih, "il2cpp_domain_assembly_open");
    p_asm_img          = (t_assembly_get_image) dlsym(ih, "il2cpp_assembly_get_image");
    p_class_from_name  = (t_class_from_name) dlsym(ih, "il2cpp_class_from_name");
    p_field_from_name  = (t_class_get_field_from_name) dlsym(ih, "il2cpp_class_get_field_from_name");
    p_field_static     = (t_field_static_get_value) dlsym(ih, "il2cpp_field_static_get_value");
    p_method_from_name = (t_class_get_method_from_name) dlsym(ih, "il2cpp_class_get_method_from_name");
    bool ok = p_domain_get && p_class_from_name;
    LOGI("il2cpp %s", ok ? "OK" : "FALLO");
    return ok;
}

static void *il2cpp_image() {
    void *dom = p_domain_get();
    void *a = p_dom_asm(dom, "Assembly-CSharp.dll");
    return a ? p_asm_img(a) : nullptr;
}
static void *il2cpp_klass(const char *ns, const char *name) {
    void *img = il2cpp_image();
    return img ? p_class_from_name(img, ns, name) : nullptr;
}
static void *il2cpp_method(const char *ns, const char *klass, const char *name, int argc) {
    void *k = il2cpp_klass(ns, klass);
    return k ? p_method_from_name(k, name, argc) : nullptr;
}
static void *il2cpp_hook(const char *klass, const char *name, int argc, void *hook, void **orig) {
    void *m = il2cpp_method("", klass, name, argc);
    if (!m) { LOGI("no existe %s::%s", klass, name); return nullptr; }
    void *fn = *(void **)m;
    if (!fn || DobbyHook(fn, hook, orig) != 0) return nullptr;
    LOGI("hook OK %s::%s", klass, name);
    return fn;
}

static void *get_transform(void *obj) {
    static void *m = nullptr;
    if (!m) m = il2cpp_method("UnityEngine", "Component", "get_transform", 0);
    if (!m || !obj) return nullptr;
    return ((void *(*)(void *)) * (void **)m)(obj);
}
static Vec3 get_pos(void *obj) {
    static void *m = nullptr;
    if (!m) m = il2cpp_method("UnityEngine", "Transform", "get_position", 0);
    if (!m) return {0, 0, 0};
    void *t = get_transform(obj);
    if (!t) return {0, 0, 0};
    return ((Vec3(*)(void *)) * (void **)m)(t);
}
static Vec3 get_rot(void *obj) {
    static void *m = nullptr;
    if (!m) m = il2cpp_method("UnityEngine", "Transform", "get_eulerAngles", 0);
    if (!m) return {0, 0, 0};
    void *t = get_transform(obj);
    if (!t) return {0, 0, 0};
    return ((Vec3(*)(void *)) * (void **)m)(t);
}
static void set_rot(void *obj, Vec3 r) {
    static void *m = nullptr;
    if (!m) m = il2cpp_method("UnityEngine", "Transform", "set_eulerAngles", 1);
    if (!m) return;
    void *t = get_transform(obj);
    if (!t) return;
    ((void (*)(void *, Vec3)) * (void **)m)(t, r);
}
static Vec3 get_fwd(void *obj) {
    Vec3 e = get_rot(obj);
    float y = e.x * PI / 180.f, p = e.y * PI / 180.f;
    return { sinf(y) * cosf(p), -sinf(p), cosf(y) * cosf(p) };
}
static void *get_local_player() {
    void *k = il2cpp_klass("", CFG::PLAYER_CLASS);
    if (!k) return nullptr;
    void *f = p_field_from_name(k, CFG::LOCAL_FIELD);
    if (!f) return nullptr;
    void *v = nullptr;
    p_field_static(f, &v);
    return v;
}

// ============================ HOOKS ============================
static int h_hitpart(void *self) {
    int o = o_hs ? ((int(*)(void*))o_hs)(self) : 0;
    if (hs_on && self == localPlayer) return 1;
    return o;
}

static float h_recoil(void *self) {
    if (rec_on) return 0.f;
    return o_rec ? ((float(*)(void*))o_rec)(self) : 0.f;
}

static void h_player(void *self) {
    if (o_aim) ((void(*)(void*))o_aim)(self);
    if (!aim_on || !localPlayer || !self || self == localPlayer) return;

    Vec3 me = get_pos(localPlayer), fwd = get_fwd(localPlayer);
    Vec3 t = get_pos(self);
    t.y += CFG::HEAD_OFFSET;

    float dx = t.x - me.x, dy = t.y - me.y, dz = t.z - me.z;
    float len = sqrtf(dx*dx + dy*dy + dz*dz);
    if (len < 0.01f) return;

    float dot = (dx*fwd.x + dy*fwd.y + dz*fwd.z) / len;
    dot = fmaxf(-1.f, fminf(1.f, dot));
    if (acosf(dot) * 180.f / PI > g_fov) return;

    Vec3 want { atan2f(dx, dz) * 180.f / PI,
               -atan2f(dy, sqrtf(dx*dx + dz*dz)) * 180.f / PI, 0.f };
    Vec3 cur = get_rot(localPlayer);
    float k = fminf(1.f, fmaxf(0.05f, g_smooth));
    set_rot(localPlayer, { cur.x + (want.x - cur.x) * k,
                           cur.y + (want.y - cur.y) * k, 0.f });
}

static void install_hooks() {
    ok_hs  = il2cpp_hook(CFG::HS_CLASS,  CFG::HS_METHOD,  CFG::HS_ARGC,  (void*)h_hitpart, &o_hs)  != nullptr;
    ok_aim = il2cpp_hook(CFG::AIM_CLASS, CFG::AIM_METHOD, CFG::AIM_ARGC, (void*)h_player,  &o_aim) != nullptr;
    ok_rec = il2cpp_hook(CFG::REC_CLASS, CFG::REC_METHOD, CFG::REC_ARGC, (void*)h_recoil,  &o_rec) != nullptr;
    LOGI("hooks hs=%d aim=%d rec=%d", ok_hs, ok_aim, ok_rec);
}

// ============================ MENU ============================
static void apply_theme() {
    ImGuiStyle &s = ImGui::GetStyle();
    s.WindowRounding = 10; s.FrameRounding = 7; s.GrabRounding = 7;
    s.WindowBorderSize = 1; s.FramePadding = ImVec2(12, 7);
    auto *c = s.Colors;
    c[ImGuiCol_WindowBg]      = ImVec4(0.03f, 0.03f, 0.04f, 0.95f);
    c[ImGuiCol_Border]        = ImVec4(1.00f, 0.10f, 0.10f, 0.70f);
    c[ImGuiCol_TitleBgActive] = ImVec4(0.78f, 0.03f, 0.03f, 1.00f);
    c[ImGuiCol_Button]        = ImVec4(0.70f, 0.03f, 0.03f, 1.00f);
    c[ImGuiCol_ButtonHovered] = ImVec4(1.00f, 0.15f, 0.15f, 1.00f);
    c[ImGuiCol_CheckMark]     = ImVec4(1.00f, 0.20f, 0.20f, 1.00f);
    c[ImGuiCol_SliderGrab]    = ImVec4(1.00f, 0.20f, 0.20f, 1.00f);
    c[ImGuiCol_Header]        = ImVec4(0.70f, 0.03f, 0.03f, 0.65f);
    c[ImGuiCol_Text]          = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
}

static void draw_menu() {
    if (!g_MenuOpen) return;
    ImGui::SetNextWindowSize(ImVec2(295, 0), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(25, 160), ImGuiCond_FirstUseEver);
    ImGui::Begin("PHANTOM", &g_MenuOpen, ImGuiWindowFlags_AlwaysAutoResize);

    ImGui::Checkbox("Auto Headshot", &hs_on);
    ImGui::Checkbox("Aim Lock",      &aim_on);
    ImGui::Checkbox("No Recoil",     &rec_on);

    ImGui::Separator();
    ImGui::SliderFloat("FOV",    &g_fov,    30.f, 180.f, "%.0f");
    ImGui::SliderFloat("Smooth", &g_smooth,  0.f,   1.f, "%.2f");

    ImGui::Separator();
    if (ImGui::CollapsingHeader("estado")) {
        ImGui::TextColored(ok_hs  ? ImVec4(.3f,1,.3f,1) : ImVec4(1,.3f,.3f,1), "%s Auto Headshot", ok_hs  ? "[ok]" : "[x]");
        ImGui::TextColored(ok_aim ? ImVec4(.3f,1,.3f,1) : ImVec4(1,.3f,.3f,1), "%s Aim Lock",      ok_aim ? "[ok]" : "[x]");
        ImGui::TextColored(ok_rec ? ImVec4(.3f,1,.3f,1) : ImVec4(1,.3f,.3f,1), "%s No Recoil",     ok_rec ? "[ok]" : "[x]");
    }
    ImGui::End();
}

// ============================ ARRANQUE ============================
static bool g_Ready = false;
static int  g_W = 0, g_H = 0;
static EGLBoolean (*o_swap)(EGLDisplay, EGLSurface) = nullptr;

static uintptr_t baseOf(const char *lib) {
    FILE *fp = fopen("/proc/self/maps", "r");
    if (!fp) return 0;
    char l[512]; uintptr_t b = 0;
    while (fgets(l, sizeof(l), fp))
        if (strstr(l, lib)) { b = strtoull(l, nullptr, 16); break; }
    fclose(fp); return b;
}
static void waitFor(const char *lib, int ms = 30000) {
    for (int i = 0; i < ms / 50 && !baseOf(lib); ++i) usleep(50000);
}

static void initImGui() {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = ImVec2((float)g_W, (float)g_H);
    apply_theme();
    ImGui_ImplOpenGL3_Init("#version 300 es");
    g_Ready = true;
    LOGI("ImGui %dx%d", g_W, g_H);
}

static EGLBoolean h_swap(EGLDisplay d, EGLSurface s) {
    if (!g_Ready) {
        eglQuerySurface(d, s, EGL_WIDTH,  &g_W);
        eglQuerySurface(d, s, EGL_HEIGHT, &g_H);
        if (g_W > 0 && g_H > 0) initImGui();
    }
    if (g_Ready) {
        if (!localPlayer) localPlayer = get_local_player();
        ImGui::GetIO().DisplaySize = ImVec2((float)g_W, (float)g_H);
        ImGui_ImplOpenGL3_NewFrame();
        ImGui::NewFrame();
        draw_menu();
        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    }
    return o_swap(d, s);
}

typedef void (*t_ime)(void *, void *, void *);
static t_ime o_ime = nullptr;

static void h_ime(void *self, void *ev, void *msg) {
    if (g_Ready && g_MenuOpen) {
        int32_t a = AMotionEvent_getAction((const AInputEvent*)ev);
        int32_t m = a & AMOTION_EVENT_ACTION_MASK;
        size_t  i = (size_t)(a >> AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT);
        ImGuiIO &io = ImGui::GetIO();
        io.AddMousePosEvent(AMotionEvent_getX((const AInputEvent*)ev, i),
                            AMotionEvent_getY((const AInputEvent*)ev, i));
        if (m == AMOTION_EVENT_ACTION_DOWN || m == AMOTION_EVENT_ACTION_POINTER_DOWN)
            io.AddMouseButtonEvent(0, true);
        else if (m == AMOTION_EVENT_ACTION_UP || m == AMOTION_EVENT_ACTION_POINTER_UP ||
                 m == AMOTION_EVENT_ACTION_CANCEL)
            io.AddMouseButtonEvent(0, false);
    }
    o_ime(self, ev, msg);
}

static void *boot(void *) {
    waitFor("libEGL.so");
    void *e = dlopen("libEGL.so", RTLD_NOW);
    DobbyHook(dlsym(e, "eglSwapBuffers"), (void*)h_swap, (void**)&o_swap);

    void *sym = DobbySymbolResolver("libinput.so",
      "_ZN7android13InputConsumer21initializeMotionEventEPNS_11MotionEventEPKNS_12InputMessageE");
    if (sym) DobbyHook(sym, (void*)h_ime, (void**)&o_ime);

    waitFor("libil2cpp.so");
    if (il2cpp_init()) install_hooks();
    LOGI("PHANTOM cargado");
    return nullptr;
}

extern "C" JNIEXPORT jint JNI_OnLoad(JavaVM *, void *) {
    pthread_t t;
    pthread_create(&t, nullptr, boot, nullptr);
    return JNI_VERSION_1_6;
}
