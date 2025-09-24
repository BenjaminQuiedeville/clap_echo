#include <string>
#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <atomic>

#define _USE_MATH_DEFINES
#include <math.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>

#include <clap/clap.h>

#include <GL/gl.h>
#include "../imgui/imgui.h"
#include "../imgui/backends/imgui_impl_opengl3.h"
#include "../imgui/backends/imgui_impl_win32.h"

typedef uint32_t u32;
typedef int32_t i32;
typedef uint64_t u64;
typedef int64_t i64;

#define global_const static const
#define local_const static const

static inline float dbtoa(float x) { return powf(10.0f, x * 0.05f); }
static inline float atodb(float x) { return 20.0f * log10f(x); }

#define memset_float(ptr, value, nelements)   memset(ptr, value, (nelements)*sizeof(float))
#define memcpy_float(dest, source, nelements) memcpy(dest, source, (nelements)*sizeof(float))
#define calloc_float(nelements)               (float*)calloc(nelements, sizeof(float))

#define CLIP(x, min, max) (x > max ? max : x < min ? min : x)

enum ParamsIndex {
    TIME,
    FEEDBACK,
    TONE_FREQ,
    MIX,
    MOD_FREQ,
    MOD_AMT,
    NPARAMS,
};

global_const char *const plugin_features[4] = {
    CLAP_PLUGIN_FEATURE_AUDIO_EFFECT,
    CLAP_PLUGIN_FEATURE_STEREO,
    CLAP_PLUGIN_FEATURE_DELAY,
    NULL,
};

global_const clap_plugin_descriptor_t pluginDescriptor {
    .clap_version = CLAP_VERSION_INIT,
    .id           = "hermes140.clap_echo",
    .name         = "Clap echo",
    .vendor       = "Hermes140",
    .url          = "",
    .manual_url   = "",
    .support_url  = "",
    .version      = "0.1",
    .description  = "Simple clap echo",

    .features = plugin_features,
};

enum ParamEventType : u32 {
    GUI_VALUE_CHANGE,
    GUI_GESTURE_BEGIN,
    GUI_GESTURE_END,
    NEVENTTYPES,
};

struct ParamEvent {
    u32 param_index = 0;
    u32 event_type = 0;
    float value = 0.0f;
};

global_const u32 FIFO_SIZE = 256;

struct EventFIFO {
    ParamEvent events[FIFO_SIZE] = {{}};
    std::atomic<u32> write_index = 0;
    std::atomic<u32> read_index = 0;
};

global_const float RAMP_TIME_MS = 100.0f;

struct RampedValue {
    float target        = 0.0f;
    float prev_target   = 0.0f;
    float step_height   = 0.0f;
    float current_value = 0.0f;
    float norm_value    = 0.0f;
    float *value_buffer = nullptr;
    bool  is_smoothing  = false;
};

struct ParamInfo {
    const char *name;
    float min = 0.0f;
    float max = 0.0f;
    float default_value = 0.0f;
    ImGuiSliderFlags imgui_flags = 0;
    u32 clap_param_flags = 0;
};

global_const ParamInfo parameter_infos[NPARAMS] = {
    {
        .name = "Delay Time", .min = 1.0f, .max = 2000.0f, .default_value = 300.0f,
        .imgui_flags = ImGuiSliderFlags_AlwaysClamp,
        .clap_param_flags = CLAP_PARAM_IS_AUTOMATABLE
    },
    {
        .name = "Feedback", .min = 0.0f, .max = 1.0f, .default_value = 0.5f,
        .imgui_flags = ImGuiSliderFlags_AlwaysClamp,
        .clap_param_flags = CLAP_PARAM_IS_AUTOMATABLE
    },
    {
        .name = "Delay Tone", .min = 500.0f, .max = 20000.0f, .default_value = 10000.0f,
        .imgui_flags = ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_Logarithmic,
        .clap_param_flags = CLAP_PARAM_IS_AUTOMATABLE
    },
    {
        .name = "Mix", .min = 0.0f, .max = 1.0f, .default_value = 0.5f,
        .imgui_flags = ImGuiSliderFlags_AlwaysClamp,
        .clap_param_flags = CLAP_PARAM_IS_AUTOMATABLE
    },
    {
        .name = "Mod Freq", .min = 0.0f, .max = 5.0f, .default_value = 1.0f,
        .imgui_flags = ImGuiSliderFlags_AlwaysClamp,
        .clap_param_flags = CLAP_PARAM_IS_AUTOMATABLE
    },
    {
        .name = "Mod Amount", .min = 0.0f, .max = 1.0f, .default_value = 0.0f,
        .imgui_flags = ImGuiSliderFlags_AlwaysClamp,
        .clap_param_flags = CLAP_PARAM_IS_AUTOMATABLE
    },
};

struct GUI {
    HWND window = nullptr;
    WNDCLASS windowClass = {};
    ImGuiContext *imgui_context = nullptr;
    HDC device_context = nullptr;
    HGLRC opengl_context = nullptr;
    u32 width = 0;
    u32 height = 0;
};

struct Onepole {
    float b0 = 0.0f;
    float a1 = 0.0f;
    float y1L = 0.0f; 
    float y1R = 0.0f;
};

struct LFO {
    float cos_value = 0.5f;
    float sin_value = 0.0f;
    float param = 0.0f;
    float *cos_buffer = nullptr;
    float *sin_buffer = nullptr;
};

struct Echo {
    float *bufferL = nullptr;
    float *bufferR = nullptr;
    u32 buffer_size = 0;
    u32 write_index = 0;
    float delay_frac = 0.0f;
};

struct PluginData {
    clap_plugin_t             plugin                       = {};
    const clap_host_t         *host                        = nullptr;
    const clap_host_params_t  *host_params                 = nullptr;
    float                     samplerate                   = 0.0f;
    u32                       min_buffer_size              = 0;
    u32                       max_buffer_size              = 0;

    RampedValue               ramped_params[NPARAMS]       = {};

    float                     audio_param_values[NPARAMS]  = {0};
    float                     main_param_values[NPARAMS]   = {0};
    bool                      param_is_in_edit[NPARAMS]    = {0};
    
    EventFIFO                 main_to_audio_fifo           = {};

    Echo    echo        = {};
    Onepole tone_filter = {};
    LFO     lfo         = {};
    GUI     gui         = {};
};


static void plugin_sync_main_to_audio(PluginData *plugin, const clap_output_events_t *out);
static void plugin_sync_audio_to_main(PluginData *plugin);
static void plugin_process_event(PluginData *plugin, const clap_event_header_t *event);
static void handle_parameter_change(PluginData *plugin, u32 param_index, float value);


static void main_push_event_to_audio(PluginData *plugin, u32 param_index, u32 event_type, float value) {

    u32 write_index = plugin->main_to_audio_fifo.write_index.load();
    
    ParamEvent *event = &plugin->main_to_audio_fifo.events[write_index];
    event->param_index = param_index; 
    event->event_type = event_type;
    event->value = value;
    
    plugin->main_to_audio_fifo.write_index.fetch_add(1);
    plugin->main_to_audio_fifo.write_index.fetch_and(FIFO_SIZE-1);
}

static inline void LFO_set_frequency(LFO *lfo, float freq, float samplerate) {
    lfo->param = 2.0f * sin(M_PI * freq/samplerate);
}

static inline void LFO_fill_buffer(LFO *lfo, u32 nsamples) {

    for (u32 index = 0; index < nsamples; index++) {
        lfo->cos_value -= lfo->param * lfo->sin_value;
        lfo->sin_value += lfo->param * lfo->cos_value;
        
        lfo->cos_buffer[index] = lfo->cos_value;
        lfo->sin_buffer[index] = lfo->sin_value;
    }
}

static inline void LFO_step_and_store(LFO *lfo, u32 index) {
    lfo->cos_value -= lfo->param * lfo->sin_value;
    lfo->sin_value += lfo->param * lfo->cos_value;

    lfo->cos_buffer[index] = lfo->cos_value;
    lfo->sin_buffer[index] = lfo->sin_value;
}

static inline void onepole_set_frequency(Onepole *f, float freq, float samplerate) {
    f->b0 = sinf(M_PI / samplerate * freq);
    f->a1 = 1.0f - f->b0;
}

static inline void set_echo_delay(Echo* echo, float delay_ms, float samplerate) {
    delay_ms = CLIP(delay_ms, parameter_infos[TIME].min, parameter_infos[TIME].max);
    echo->delay_frac = delay_ms * 0.001f * samplerate;
}

static inline float echo_read_sample(float *echo_buffer, u32 buffer_size, float read_position_frac) {

    if (read_position_frac < 0.0f) { read_position_frac += (float)buffer_size; }

    i32 read_index1 = (i32)read_position_frac;
    i32 read_index2 = read_index1 - 1;
    
    if (read_index2 < 0) { read_index2 += buffer_size; }
    
    float interp_coeff = read_position_frac - (float)read_index1;
    float sample1 = echo_buffer[read_index1];
    float sample2 = echo_buffer[read_index2];
    
    float output_sample = sample1 * (1.0f - interp_coeff) + sample2 * interp_coeff;
    return output_sample;
}


static void ramped_value_init(RampedValue *value, float init_value, u32 value_buffer_size) {
    value->target = init_value;
    value->prev_target = init_value;
    value->step_height = 0.0f;
    value->current_value = init_value;
    value->norm_value = 0.0f;
    value->value_buffer = calloc_float(value_buffer_size);
    value->is_smoothing = false;
}

static void ramped_value_new_target(RampedValue *value, float new_target, float samplerate) {
    value->prev_target = value->target;
    value->target = new_target;
    value->step_height = 1.0f / (RAMP_TIME_MS * 0.001f * samplerate);
    value->norm_value = 0.0f;
    value->is_smoothing = true;
}

static float ramped_value_step(RampedValue* value) {

    if (value->current_value == value->target) {
        value->is_smoothing = false;
        return value->current_value;
    }

    value->norm_value += value->step_height;
    if (value->norm_value >= 1.0f) {
        value->norm_value = 1.0f;
        value->current_value = value->target;
        return value->current_value;
    }

    value->current_value = value->current_value * (value->target - value->prev_target) + value->prev_target;
    return value->current_value;
}

static void ramped_value_fill_buffer(RampedValue *value, u32 nsamples) {

    float target = value->target;
    float prev_target = value->prev_target;
    float step_height = value->step_height;
    float current_value = value->current_value;
    float norm_value = value->norm_value;


    if (current_value == target) {
        for (u32 index = 0; index < nsamples; index++) {
            value->value_buffer[index] = current_value;
        }
        value->is_smoothing = false;
        return;
    }

    for (u32 index = 0; index < nsamples; index++) {
        if (current_value == target) {
            value->value_buffer[index] = current_value;
            continue;
        }

        norm_value += step_height;
        if (norm_value >= 1.0f) {
            norm_value = 1.0f;
            current_value = target;
            value->value_buffer[index] = current_value;
            continue;
        }

        current_value = norm_value * (target - prev_target) + prev_target;
        value->value_buffer[index] = current_value;
    }

    value->current_value = current_value;
    value->norm_value = norm_value;
}


// audio ports plugin extension

static u32 get_audio_ports_count(const clap_plugin_t *plugin, bool isInput) {
    return 1;
}

static bool get_audio_ports_info(const clap_plugin_t *plugin, u32 index, bool isInput, clap_audio_port_info_t *info) {
    assert(index < 2);
    if (isInput) {
        info->id = 0;
        info->channel_count = 2;
        info->flags = CLAP_AUDIO_PORT_IS_MAIN;
        info->port_type = CLAP_PORT_STEREO;
        info->in_place_pair = CLAP_INVALID_ID;
        snprintf(info->name, sizeof(info->name), "%s", "Audio Input");

    } else {

        info->id = 0;
        info->channel_count = 2;
        info->flags = CLAP_AUDIO_PORT_IS_MAIN;
        info->port_type = CLAP_PORT_STEREO;
        info->in_place_pair = CLAP_INVALID_ID;
        snprintf(info->name, sizeof(info->name), "%s", "Audio Output");
    }
    return true;
}

global_const clap_plugin_audio_ports_t extensionAudioPorts = {
    .count = get_audio_ports_count,
    .get = get_audio_ports_info,
};


// parameter plugin extension

u32 get_num_params(const clap_plugin_t *plugin) { return NPARAMS; }

static bool params_get_info(const clap_plugin_t *_plugin, u32 index, clap_param_info_t *information) {

    if (index >= NPARAMS) { return false; }

    memset(information, 0, sizeof(*information));
    information->id = index;
    information->flags = CLAP_PARAM_IS_AUTOMATABLE;
    information->min_value = parameter_infos[index].min;
    information->max_value = parameter_infos[index].max;
    information->default_value = parameter_infos[index].default_value;
    strcpy_s(information->name, parameter_infos[index].name);
    return true;
}

static bool param_get_value(const clap_plugin_t *_plugin, clap_id id, double *value) {
    PluginData *plugin = (PluginData*)_plugin->plugin_data;
    u32 param_index = (u32)id;

    if (param_index >= NPARAMS) { return false; }

    // not thread safe
    *value = (double)plugin->audio_param_values[param_index];
    return true;
}

static bool param_convert_value_to_text(const clap_plugin_t *_plugin, clap_id id, double value, char *display, u32 size) {
    u32 param_index = (u32) id;

    switch (param_index) {
        case TIME: {
            snprintf(display, size, "%f ms", value);
            return true;
        }
        case MOD_AMT:
        case FEEDBACK:
        case MIX: {
            snprintf(display, size, "%f", value);
            return true;
        }
        case TONE_FREQ:
        case MOD_FREQ: {
            snprintf(display, size, "%f Hz", value);
            return true;
        }
        case NPARAMS:
        default: {
            return false;
        }
    }
}

static bool param_convert_text_to_value(const clap_plugin_t *_plugin, clap_id param_id, const char *display, double *value) {

    u32 param_index = (u32)param_id;
    if (param_index >= NPARAMS) { return false; }

    *value = (double)atoi(display);
    return true;
}

// synchronise et vide les queues d'events entre le plugin et le host
static void param_flush(const clap_plugin_t *_plugin, const clap_input_events_t *in, const clap_output_events_t *out) {
    PluginData *plugin = (PluginData*)_plugin->plugin_data;
    const u32 event_count = in->size(in);

    plugin_sync_main_to_audio(plugin, out);

    for (u32 event_index = 0; event_index < event_count; event_index++) {
        plugin_process_event(plugin, in->get(in, event_index));
    }
}


global_const clap_plugin_params_t extensionParams = {
    .count = get_num_params,
    .get_info = params_get_info,
    .get_value = param_get_value,
    .value_to_text = param_convert_value_to_text,
    .text_to_value = param_convert_text_to_value,
    .flush = param_flush
};


// state plugin extension

static bool plugin_state_save(const clap_plugin_t *_plugin, const clap_ostream_t *stream) {
    PluginData *plugin = (PluginData*)_plugin->plugin_data;
    plugin_sync_audio_to_main(plugin);

    u32 num_params_written = stream->write(stream, plugin->main_param_values, sizeof(float)*NPARAMS);

    return num_params_written == sizeof(float) * NPARAMS;
}

static bool plugin_state_load(const clap_plugin_t *_plugin, const clap_istream_t *stream) {
    PluginData *plugin = (PluginData*)_plugin->plugin_data;

    // not thread safe
    u32 num_params_read = stream->read(stream, plugin->main_param_values, sizeof(float)* NPARAMS);
    bool success = num_params_read == sizeof(float)*NPARAMS;
    return success;
}

global_const clap_plugin_state_t extensionState = {
    .save = plugin_state_save,
    .load = plugin_state_load,
};


// GUI
global_const u32 GUI_WIDTH = 300;
global_const u32 GUI_HEIGHT = 200;
global_const char *GUI_API = CLAP_WINDOW_API_WIN32;

// Helper functions
static bool CreateDeviceWGL(GUI *gui) {

    HDC hDc = ::GetDC(gui->window);
    PIXELFORMATDESCRIPTOR pfd = { 0 };
    pfd.nSize = sizeof(pfd);
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 32;

    const int pf = ::ChoosePixelFormat(hDc, &pfd);
    if (pf == 0) {
        return false;
    }
    if (::SetPixelFormat(hDc, pf, &pfd) == FALSE) {
        return false;
    }
    ::ReleaseDC(gui->window, hDc);

    gui->device_context = ::GetDC(gui->window);
    if (!gui->opengl_context) {
        gui->opengl_context = wglCreateContext(gui->device_context);
    }
    return true;
}

static void CleanupDeviceWGL(GUI *gui) {
    wglMakeCurrent(nullptr, nullptr);
    ::ReleaseDC(gui->window, gui->device_context);
}

static void make_slider(PluginData *plugin, u32 param_index, const char* format) {
    
    bool slider_has_changed = ImGui::SliderFloat(parameter_infos[param_index].name,
                                                 &plugin->main_param_values[param_index],
                                                 parameter_infos[param_index].min,
                                                 parameter_infos[param_index].max,
                                                 format, parameter_infos[param_index].imgui_flags);

    if (slider_has_changed) {
    
        if (!plugin->param_is_in_edit[param_index]) {
            plugin->param_is_in_edit[param_index] = true;
            
            main_push_event_to_audio(plugin, param_index, GUI_GESTURE_BEGIN, plugin->main_param_values[param_index]);
        }
        
        main_push_event_to_audio(plugin, param_index, GUI_VALUE_CHANGE, plugin->main_param_values[param_index]);
    
    } else {
        if (plugin->param_is_in_edit[param_index]) {
            plugin->param_is_in_edit[param_index] = false;
            
            main_push_event_to_audio(plugin, param_index, GUI_GESTURE_END, plugin->main_param_values[param_index]);
        }
    }
}


extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

LRESULT CALLBACK GUIWindowProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    PluginData *plugin = (PluginData *) GetWindowLongPtr(window, 0);

    if (!plugin) {
        return DefWindowProc(window, message, wParam, lParam);
    }

    GUI *gui = &plugin->gui;
    ImGui::SetCurrentContext(gui->imgui_context);

    if (ImGui_ImplWin32_WndProcHandler(window, message, wParam, lParam)) {
        return true;
    }

    switch (message) {
        case WM_TIMER: {
            ImGui::SetCurrentContext(gui->imgui_context);
            wglMakeCurrent(gui->device_context, gui->opengl_context);
    
            plugin_sync_audio_to_main(plugin);

            if (IsIconic(gui->window)) {
                Sleep(10);
                break;
            }

            ImGui_ImplOpenGL3_NewFrame();
            ImGui_ImplWin32_NewFrame();
            ImGui::NewFrame();

            ImGuiViewport* viewport = ImGui::GetMainViewport();
            ImGui::SetNextWindowPos(viewport->WorkPos);
            ImGui::SetNextWindowSize(viewport->WorkSize);

            // ImGuiIO& io = ImGui::GetIO();

            {
                bool open = true;
                ImGui::Begin("Clap Echo", &open, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoDecoration);

                make_slider(plugin, TIME,      "%.2f ms");
                make_slider(plugin, FEEDBACK,  "%.2f");
                make_slider(plugin, TONE_FREQ, "%.1f Hz");
                make_slider(plugin, MIX,       "%.2f");
                make_slider(plugin, MOD_FREQ,  "%.2f Hz");
                make_slider(plugin, MOD_AMT,   "%.2f");

                if (ImGui::Button("Clear buffers")) {
                    memset_float(plugin->echo.bufferL, 0, plugin->echo.buffer_size*2);
                }
                
                ImGui::End();
            }

            ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.6f, 1.0f);
            ImGui::Render();
            glViewport(0, 0, gui->width, gui->height);
            glClearColor(clear_color.x, clear_color.y, clear_color.z, clear_color.w);
            glClear(GL_COLOR_BUFFER_BIT);
            ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

            SwapBuffers(gui->device_context);

            return 0;
        }
        default: {
            return DefWindowProc(window, message, wParam, lParam);
        }
    }

    return 0;
}


static bool is_gui_api_supported(const clap_plugin_t *plugin, const char *api, bool is_floating) {
    return 0 == strcmp(api, GUI_API) && !is_floating;
}

static bool gui_get_preferred_api(const clap_plugin_t *plugin, const char **api, bool *is_floating) {
    *api = GUI_API;
    *is_floating = false;
    return true;
}

static bool create_gui(const clap_plugin_t *_plugin, const char *api, bool is_floating) {
    if (!is_gui_api_supported(_plugin, api, is_floating)) {
        return false;
    }

    PluginData* plugin = (PluginData*)_plugin->plugin_data;

    plugin->gui.windowClass = {};

    memset(&plugin->gui.windowClass, 0, sizeof(plugin->gui.windowClass));
    plugin->gui.windowClass.lpfnWndProc = GUIWindowProcedure;
    plugin->gui.windowClass.cbWndExtra = sizeof(PluginData *);
    plugin->gui.windowClass.lpszClassName = pluginDescriptor.id;
    plugin->gui.windowClass.hCursor = LoadCursor(NULL, IDC_ARROW);
    plugin->gui.windowClass.style = CS_OWNDC | CS_DBLCLKS;
    RegisterClass(&plugin->gui.windowClass);

    plugin->gui.window = CreateWindow(pluginDescriptor.id,
                                      pluginDescriptor.name,
                                      WS_CHILDWINDOW | WS_CLIPSIBLINGS,
                                      CW_USEDEFAULT, 0,
                                      GUI_WIDTH, GUI_HEIGHT,
                                      GetDesktopWindow(),
                                      NULL,
                                      plugin->gui.windowClass.hInstance,
                                      NULL);

    SetWindowLongPtr(plugin->gui.window, 0, (LONG_PTR) plugin);

    plugin->gui.width = GUI_WIDTH;
    plugin->gui.height = GUI_HEIGHT;
    return true;
}

static void destroy_gui(const clap_plugin_t *_plugin) {
    PluginData* plugin = (PluginData*)_plugin->plugin_data;

    DestroyWindow(plugin->gui.window);
    plugin->gui.window = nullptr;

    UnregisterClass(pluginDescriptor.id, NULL);
}

static bool set_gui_scale(const clap_plugin_t *_plugin, double scale) {
    return false;
}

static bool get_gui_size(const clap_plugin_t *_plugin, u32* width, u32* height) {
    *width = GUI_WIDTH;
    *height = GUI_HEIGHT;
    return true;
}

static bool can_gui_resize(const clap_plugin_t *_plugin) {
    return false;
}

static bool get_gui_resize_hints(const clap_plugin_t *_plugin, clap_gui_resize_hints_t *hints) {
    return false;
}

static bool adjust_gui_size(const clap_plugin_t *_plugin, u32 *width, u32 *height) {
    return get_gui_size(_plugin, width, height);
}

static bool set_gui_size(const clap_plugin_t *_plugin, u32 width, u32 height) {
    return true;
}

static bool set_gui_parent(const clap_plugin_t *_plugin, const clap_window_t *parent_window) {
    assert(0 == strcmp(parent_window->api, GUI_API));

    PluginData *plugin = (PluginData*)_plugin->plugin_data;

    SetParent(plugin->gui.window, (HWND)parent_window->win32);
    return true;
}

static bool set_gui_transient(const clap_plugin_t *_plugin, const clap_window_t *window) {
    return false;
}

static void suggest_gui_title(const clap_plugin_t *_plugin, const char *title) {}

static bool show_gui(const clap_plugin_t *_plugin) {
    PluginData *plugin =(PluginData*)_plugin->plugin_data;
    GUI *gui = &plugin->gui;

    ShowWindow(gui->window, SW_SHOW);
    SetFocus(gui->window);

    if (!CreateDeviceWGL(gui)) {
        CleanupDeviceWGL(gui);
        DestroyWindow(gui->window);
        UnregisterClass(pluginDescriptor.id, NULL);
    }

    wglMakeCurrent(gui->device_context, gui->opengl_context);

    UpdateWindow(gui->window);

    IMGUI_CHECKVERSION();
    ImGui::SetCurrentContext(nullptr);
    gui->imgui_context = ImGui::CreateContext();
    ImGui::SetCurrentContext(gui->imgui_context);

    ImGuiIO& io = ImGui::GetIO(); (void)io;
    ImGui::StyleColorsDark();

    ImGui_ImplWin32_InitForOpenGL(gui->window);
    ImGui_ImplOpenGL3_Init();

    SetTimer(gui->window, 1, 30, nullptr);

    return true;
}

static bool hide_gui(const clap_plugin_t *_plugin) {
    PluginData *plugin =(PluginData*)_plugin->plugin_data;
    GUI *gui = &plugin->gui;

    ShowWindow(gui->window, SW_HIDE);
    SetFocus(gui->window);

    wglMakeCurrent(gui->device_context, gui->opengl_context);
    ImGui::SetCurrentContext(gui->imgui_context);

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    gui->imgui_context = nullptr;

    CleanupDeviceWGL(gui);
    wglDeleteContext(gui->opengl_context);
    gui->opengl_context = nullptr;
    gui->device_context = nullptr;

    KillTimer(gui->window, 1);

    return true;
}


global_const clap_plugin_gui_t extensionGUI = {
    .is_api_supported = is_gui_api_supported,
    .get_preferred_api = gui_get_preferred_api,
    .create = create_gui,
    .destroy = destroy_gui,
    .set_scale = set_gui_scale,
    .get_size = get_gui_size,
    .can_resize = can_gui_resize,
    .get_resize_hints = get_gui_resize_hints,
    .adjust_size = adjust_gui_size,
    .set_size = set_gui_size,
    .set_parent = set_gui_parent,
    .set_transient = set_gui_transient,
    .suggest_title = suggest_gui_title,
    .show = show_gui,
    .hide = hide_gui,
};

// main plugin class

static void plugin_sync_main_to_audio(PluginData *plugin, const clap_output_events_t *out) {

    u32 read_index = plugin->main_to_audio_fifo.read_index.load();
    u32 write_index = plugin->main_to_audio_fifo.write_index.load();
        
    while (read_index != write_index) {
    
        ParamEvent *plugin_event = &plugin->main_to_audio_fifo.events[read_index];
        
        switch (plugin_event->event_type) {
            case GUI_VALUE_CHANGE: {
                
                handle_parameter_change(plugin, plugin_event->param_index, plugin_event->value);
            
                clap_event_param_value_t clap_event = {};
                clap_event.header.size = sizeof(clap_event);
                clap_event.header.time = 0;
                clap_event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
                clap_event.header.type = CLAP_EVENT_PARAM_VALUE;
                clap_event.header.flags = 0;
                clap_event.param_id = plugin_event->param_index;
                clap_event.cookie = NULL;
                clap_event.note_id = -1;
                clap_event.port_index = -1;
                clap_event.channel = -1;
                clap_event.key = -1;
                clap_event.value = plugin_event->value;
                out->try_push(out, &clap_event.header);                
                break;
            }
            case GUI_GESTURE_BEGIN: {
                clap_event_param_gesture_t clap_event = {};
                clap_event.header.size = sizeof(clap_event);
                clap_event.header.time = 0;
                clap_event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
                clap_event.header.type = CLAP_EVENT_PARAM_GESTURE_BEGIN;
                clap_event.header.flags = 0;
                clap_event.param_id = plugin_event->param_index;
                out->try_push(out, &clap_event.header);
                break;
            }
            case GUI_GESTURE_END: {
                clap_event_param_gesture_t clap_event = {};
                clap_event.header.size = sizeof(clap_event);
                clap_event.header.time = 0;
                clap_event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
                clap_event.header.type = CLAP_EVENT_PARAM_GESTURE_END;
                clap_event.header.flags = 0;
                clap_event.param_id = plugin_event->param_index;
                out->try_push(out, &clap_event.header);                
                break;
            }
            default: { break; }
        }
    
        read_index++;
        read_index &= (FIFO_SIZE - 1);
    }
    
    plugin->main_to_audio_fifo.read_index.store(read_index);
}


static void plugin_sync_audio_to_main(PluginData *plugin) {

    for (u32 param_index = 0; param_index < NPARAMS; param_index++) {
        plugin->main_param_values[param_index] = plugin->audio_param_values[param_index];
    }
}

static void plugin_process_event(PluginData *plugin, const clap_event_header_t *event) {
    if (event->space_id == CLAP_CORE_EVENT_SPACE_ID) {
        if (event->type == CLAP_EVENT_PARAM_VALUE) {
            const clap_event_param_value_t *param_event = (clap_event_param_value_t*)event;

            handle_parameter_change(plugin, param_event->param_id, (float)param_event->value);
        }
    }
}

static void handle_parameter_change(PluginData *plugin, u32 param_index, float value) {
    
    plugin->audio_param_values[param_index] = value;
    ramped_value_new_target(&plugin->ramped_params[param_index], value, plugin->samplerate);
}


static clap_process_status plugin_class_process(const clap_plugin *_plugin, const clap_process_t *process) {
    PluginData *plugin = (PluginData*)_plugin->plugin_data;


    plugin_sync_main_to_audio(plugin, process->out_events);


    assert(process->audio_outputs_count == 1);
    assert(process->audio_inputs_count == 1);

    const u32 frame_count = process->frames_count;
    const u32 input_event_count = process->in_events->size(process->in_events);

    u32 event_index = 0;
    u32 next_event_frame = input_event_count ? 0 : frame_count;

    for (u32 current_frame_index = 0; current_frame_index < frame_count;) {
        while (event_index < input_event_count && next_event_frame == current_frame_index) {
            const clap_event_header_t *event = process->in_events->get(process->in_events, event_index);

            if (event->time != current_frame_index) {
                next_event_frame = event->time;
                break;
            } 
            
            plugin_process_event(plugin, event);
            event_index++;

            if (event_index == input_event_count) {
                next_event_frame = frame_count;
                break;
            }
        }

        { // do the audio render 
            const u32 nsamples = next_event_frame - current_frame_index;
        
            float *inputL = &process->audio_inputs[0].data32[0][current_frame_index];
            float *inputR = &process->audio_inputs[0].data32[1][current_frame_index];
        
            float *outputL = &process->audio_outputs[0].data32[0][current_frame_index];
            float *outputR = &process->audio_outputs[0].data32[1][current_frame_index];
        
            // generate ramped_value buffer
        
            for (u32 param_index = 0; param_index < NPARAMS; param_index++) {
                ramped_value_fill_buffer(&plugin->ramped_params[param_index], nsamples);
            }
        
            if (plugin->ramped_params[MOD_FREQ].is_smoothing) {
                for (u32 index = 0; index < nsamples; index++) {
                    LFO_set_frequency(&plugin->lfo, plugin->ramped_params[MOD_FREQ].value_buffer[index], plugin->samplerate);
                    LFO_step_and_store(&plugin->lfo, index);
                }
            } else {
                LFO_fill_buffer(&plugin->lfo, nsamples);
            }
        
        
            for (u32 index = 0; index < nsamples; index++) {
        
                Echo *echo = &plugin->echo;
        
                if (plugin->ramped_params[TIME].is_smoothing) {
                    set_echo_delay(echo, plugin->ramped_params[TIME].value_buffer[index], plugin->samplerate);
                }
        
                if (plugin->ramped_params[TONE_FREQ].is_smoothing) {
                    onepole_set_frequency(&plugin->tone_filter, plugin->ramped_params[TONE_FREQ].value_buffer[index], plugin->samplerate);
                }
        
                float feedback = plugin->ramped_params[FEEDBACK].value_buffer[index];
                float mix = plugin->ramped_params[MIX].value_buffer[index];
                
                local_const float amout_scale = 200.0f;
                float mod_amount = plugin->ramped_params[MOD_AMT].value_buffer[index] * amout_scale;
                float mod_valueL = plugin->lfo.cos_buffer[index] * mod_amount;
                float mod_valueR = plugin->lfo.sin_buffer[index] * mod_amount;
                
                // bien vérifier que la tete de lecture sorte pas du buffer (mettre des asserts)
                float read_index_frac = (float)echo->write_index - echo->delay_frac;
                float output_sampleL = echo_read_sample(echo->bufferL, echo->buffer_size, read_index_frac - mod_valueL);
                float output_sampleR = echo_read_sample(echo->bufferR, echo->buffer_size, read_index_frac - mod_valueR);
        
                {
                    float b0 = plugin->tone_filter.b0;
                    float a1 = plugin->tone_filter.a1;
                    
                    output_sampleL = output_sampleL * b0 + plugin->tone_filter.y1L * a1;
                    plugin->tone_filter.y1L = output_sampleL;
                    
                    output_sampleR = output_sampleR * b0 + plugin->tone_filter.y1R * a1;
                    plugin->tone_filter.y1R = output_sampleR;        
                }
                
                float input_sampleL = inputL[index];
                float input_sampleR = inputR[index];
        
                outputL[index] = output_sampleL * mix + input_sampleL * (1.0f - mix);
                outputR[index] = output_sampleR * mix + input_sampleR * (1.0f - mix);
                
                // saturer sur demande le feedback (c'est drole)        
                echo->bufferL[echo->write_index] = input_sampleL + output_sampleL*feedback;
                echo->bufferR[echo->write_index] = input_sampleR + output_sampleR*feedback;
        
                echo->write_index++;
                if (echo->write_index == echo->buffer_size) {
                    echo->write_index = 0;
                }
            }
        }
        current_frame_index = next_event_frame;
    }

    return CLAP_PROCESS_CONTINUE;
}

static bool plugin_class_init(const clap_plugin *_plugin)  {
    PluginData *plugin = (PluginData*)_plugin->plugin_data;
    for (u32 param_index = 0; param_index < NPARAMS; param_index++) {
        clap_param_info_t information = {0};
        extensionParams.get_info(_plugin, param_index, &information);
        plugin->main_param_values[param_index] = information.default_value;
        plugin->audio_param_values[param_index] = information.default_value;
    }

    plugin->host_params = (const clap_host_params_t*)plugin->host->get_extension(plugin->host, CLAP_EXT_PARAMS);

    return true;
}

static void plugin_class_destroy(const clap_plugin *_plugin) {
    PluginData *plugin = (PluginData*)_plugin->plugin_data;
    free(plugin);
}

static bool plugin_class_activate(const clap_plugin *_plugin, double samplerate, u32 min_buffer_size, u32 max_buffer_size) {
    PluginData *plugin = (PluginData*)_plugin->plugin_data;
    plugin->samplerate = samplerate;
    plugin->min_buffer_size = min_buffer_size;
    plugin->max_buffer_size = max_buffer_size;

    for (u32 param_index = 0; param_index < NPARAMS; param_index++) {
        ramped_value_init(&plugin->ramped_params[param_index], parameter_infos[param_index].default_value, max_buffer_size);
    }

    {
        Echo *echo = &plugin->echo;

        echo->buffer_size = (u32)(parameter_infos[TIME].max * 0.001f * samplerate);
        echo->bufferL = calloc_float(echo->buffer_size * 2);
        assert(echo->bufferL && "Problem during echo buffer allocation");

        echo->bufferR = &echo->bufferL[echo->buffer_size];

        echo->write_index = 0;
        echo->delay_frac = 0;
        
        set_echo_delay(echo, plugin->audio_param_values[TIME], samplerate);
    }

    onepole_set_frequency(&plugin->tone_filter, plugin->audio_param_values[TONE_FREQ], samplerate);

    LFO_set_frequency(&plugin->lfo, plugin->audio_param_values[MOD_FREQ], samplerate);
    plugin->lfo.cos_buffer = calloc_float(max_buffer_size*2);
    plugin->lfo.sin_buffer = plugin->lfo.cos_buffer + max_buffer_size;
    plugin->lfo.cos_value = 0.5f;
    plugin->lfo.sin_value = 0.0f;
    
    return true;
}

static void plugin_class_deactivate(const clap_plugin *_plugin) {

    PluginData *plugin = (PluginData*)_plugin->plugin_data;

    free(plugin->echo.bufferL);
    plugin->echo.bufferL = nullptr;
    plugin->echo.bufferR = nullptr;
    
    free(plugin->lfo.cos_buffer);
    plugin->lfo.cos_buffer = nullptr;
    plugin->lfo.sin_buffer = nullptr;

}

static bool plugin_class_start_processing(const clap_plugin *_plugin) {
    return true;
}

static void plugin_class_stop_processing(const clap_plugin *_plugin) {}

static void plugin_class_reset(const clap_plugin *_plugin) {
    PluginData *plugin = (PluginData*)_plugin->plugin_data;
    (void)plugin;
}

static const void *plugin_class_get_extension(const clap_plugin *_plugin, const char *id) {
    if (0 == strcmp(id, CLAP_EXT_AUDIO_PORTS))  { return &extensionAudioPorts; }
    if (0 == strcmp(id, CLAP_EXT_PARAMS))       { return &extensionParams; }
    if (0 == strcmp(id, CLAP_EXT_STATE))        { return &extensionState; }
    if (0 == strcmp(id, CLAP_EXT_GUI))          { return &extensionGUI; }

    return nullptr;
}

static void plugin_class_on_main_thread(const clap_plugin *_plugin) {}


global_const clap_plugin_t pluginClass {
    .desc             = &pluginDescriptor,
    .plugin_data      = nullptr,
    .init             = plugin_class_init,
    .destroy          = plugin_class_destroy,
    .activate         = plugin_class_activate,
    .deactivate       = plugin_class_deactivate,
    .start_processing = plugin_class_start_processing,
    .stop_processing  = plugin_class_stop_processing,
    .reset            = plugin_class_reset,
    .process          = plugin_class_process,
    .get_extension    = plugin_class_get_extension,
    .on_main_thread   = plugin_class_on_main_thread,
};

// plugin factory

u32 get_plugin_count(const clap_plugin_factory_t *factory) { return 1; }

const clap_plugin_descriptor_t* get_plugin_descriptor(const clap_plugin_factory_t *factory, u32 index) {
    return index == 0 ? &pluginDescriptor : nullptr;
}

const clap_plugin_t* create_plugin(const clap_plugin_factory_t *factory, const clap_host_t *host, const char *pluginID) {
    if (!clap_version_is_compatible(host->clap_version) || strcmp(pluginID, pluginDescriptor.id)) {
        return nullptr;
    }

    PluginData *plugin = (PluginData*)calloc(1, sizeof(PluginData));
    plugin->host = host;
    plugin->plugin = pluginClass;
    plugin->plugin.plugin_data = plugin;
    return &plugin->plugin;
}

global_const clap_plugin_factory_t pluginFactory {
    .get_plugin_count = get_plugin_count,
    .get_plugin_descriptor = get_plugin_descriptor,
    .create_plugin = create_plugin,
};


// plugin entry

bool lib_init(const char *path) { return true; }
void lib_deinit() {}
const void* lib_get_factory(const char *id) {
    return strcmp(id, CLAP_PLUGIN_FACTORY_ID) ? nullptr : &pluginFactory;
}
