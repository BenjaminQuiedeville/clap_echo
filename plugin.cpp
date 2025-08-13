#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <math.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>

#include <clap/clap.h>

#include <imgui.cpp>
#include <imgui_draw.cpp>
#include <imgui_widgets.cpp>
#include <imgui_tables.cpp>

#include <backends/imgui_impl_win32.cpp>
#include <backends/imgui_impl_opengl3.cpp>

typedef uint32_t u32;
typedef int32_t i32;
typedef uint64_t u64;
typedef int64_t i64;

#define global_const static const
#define local_const static const

global_const u32 MAX_EVENTS = 256;

typedef u32 Events;

struct EventFIFO {    
    Events events[MAX_EVENTS] = {0};
    u32 push_index = 0;
    u32 pop_index = 0;
};

// fifo_push_event(fifo, event);
// event = fifo_pop_event(fifo);


enum ParamsIndex {
    Time, 
    Feedback, 
    ToneFreq,
    Mix, 
    ModFreq, 
    ModAmount, 
    NParams,
};

global_const char *const plugin_features[4] = {
    CLAP_PLUGIN_FEATURE_AUDIO_EFFECT,
    CLAP_PLUGIN_FEATURE_STEREO,
    CLAP_PLUGIN_FEATURE_DELAY,
    NULL,
};

global_const clap_plugin_descriptor_t pluginDescriptor {
    .clap_version = CLAP_VERSION_INIT,
    .id          = "hermes140.clap_echo",       // eg: "com.u-he.diva", mandatory
    .name        = "Clap echo",                 // eg: "Diva", mandatory
    .vendor      = "Hermes140",                 // eg: "u-he"
    .url         = "",                          // eg: "https://u-he.com/products/diva/"
    .manual_url  = "",                          // eg: "https://dl.u-he.com/manuals/plugins/diva/Diva-user-guide.pdf"
    .support_url = "",                          // eg: "https://u-he.com/support/"
    .version     = "0.1",                       // eg: "1.4.4"
    .description = "Simple clap echo",          // eg: "The spirit of analogue"
    
    // Arbitrary list of keywords.
    // They can be matched by the host indexer and used to classify the plugin.
    // The array of pointers must be null terminated.
    // For some standard features see plugin-features.h
    .features = plugin_features,
};


struct GUI {
    HWND window = nullptr;
    WNDCLASS windowClass = {};
    ImGuiContext *imgui_context = nullptr;
    HDC device_context = nullptr;
    HGLRC opengl_context = nullptr;
    float ui_value1 = 0.0f;
    float ui_value2 = 0.0f;
    u32 width = 0;
    u32 height = 0;
};

struct MyPlugin {
    clap_plugin_t     plugin                       = {};
    const clap_host_t *host                        = nullptr;
    float             samplerate                   = 0.0f; 
    float             audio_param_values[NParams]  = {0}; 
    float             main_param_values[NParams]   = {0};
    bool              audio_param_changed[NParams] = {0};
    bool              main_param_changed[NParams]  = {0};
    
    GUI gui = {};
};

static void PluginSyncMainToAudio(MyPlugin *plugin, const clap_output_events_t *out);
static bool PluginSyncAudioToMain(MyPlugin *plugin);
static void PluginProcessEvent(MyPlugin *plugin, const clap_event_header_t *event);
static void PluginRenderAudio(MyPlugin *plugin, u32 start, u32 end, float **inputs, float **outputs);


// audio ports plugin extension

u32 get_audio_ports_count(const clap_plugin_t *plugin, bool isInput) {
    return isInput ? 1 : 1;
}

bool get_audio_ports_info(const clap_plugin_t *plugin, u32 index, bool isInput, clap_audio_port_info_t *info) {
    assert(index < 2);
    if (isInput) { 
        info->id = 0;
        info->channel_count = 2;
        info->flags = CLAP_AUDIO_PORT_IS_MAIN;
        info->port_type = CLAP_PORT_STEREO;
        info->in_place_pair = CLAP_INVALID_ID;
        snprintf(info->name, sizeof(info->name), "%s", "Audio Input");
        
    } else {
    
        info->id = 1;
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

u32 get_num_params(const clap_plugin_t *plugin) { return NParams; }

bool params_get_info(const clap_plugin_t *_plugin, u32 index, clap_param_info_t *information) {
    
    switch (index) {
        case Time: {
            memset(information, 0, sizeof(*information));
            information->id = index;
            information->flags = CLAP_PARAM_IS_AUTOMATABLE;
            information->min_value = 1.0f;
            information->max_value = 2000.0f;
            information->default_value = 300.0f;
            strcpy_s(information->name, "Delay Time (ms)");                
            return true;
        } 
        case Feedback: {
            memset(information, 0, sizeof(*information));
            information->id = index;
            information->flags = CLAP_PARAM_IS_AUTOMATABLE;
            information->min_value = 0.0f;
            information->max_value = 1.0f;
            information->default_value = 0.5f;
            strcpy_s(information->name, "Feedback");             
            return true;
        } 
        case ToneFreq: {
            memset(information, 0, sizeof(*information));
            information->id = index;
            information->flags = CLAP_PARAM_IS_AUTOMATABLE;
            information->min_value = 500.0f;
            information->max_value = 20000.0f;
            information->default_value = 10000.0f;
            strcpy_s(information->name, "Delay Tone");                
            return true;
        }
        case Mix: {
            memset(information, 0, sizeof(*information));
            information->id = index;
            information->flags = CLAP_PARAM_IS_AUTOMATABLE;
            information->min_value = 0.0f;
            information->max_value = 1.0f;
            information->default_value = 0.3f;
            strcpy_s(information->name, "Mix");                
            return true;
        } 
        case ModFreq: {
            memset(information, 0, sizeof(*information));
            information->id = index;
            information->flags = CLAP_PARAM_IS_AUTOMATABLE;
            information->min_value = 0.0f;
            information->max_value = 5.0f;
            information->default_value = 1.0f;
            strcpy_s(information->name, "Mod Freq");                
            return true;
        } 
        case ModAmount: {
            memset(information, 0, sizeof(*information));
            information->id = index;
            information->flags = CLAP_PARAM_IS_AUTOMATABLE;
            information->min_value = 0.0f;
            information->max_value = 1.0f;
            information->default_value = 0.0f;
            strcpy_s(information->name, "Mod Amount");                
            return true;
        } 
        case NParams:
        default: { return false; }
    }
    
    return false; 
}

bool param_get_value(const clap_plugin_t *_plugin, clap_id id, double *value) {
    MyPlugin *plugin = (MyPlugin*)_plugin->plugin_data;
    u32 param_index = (u32)id;
    
    if (param_index >= NParams) { return false; }
    
    // not thread safe
    *value = plugin->main_param_changed[param_index] ? (double)plugin->main_param_values[param_index]
                                                     : (double)plugin->audio_param_values[param_index];
    return true;
}

bool param_convert_value_to_text(const clap_plugin_t *_plugin, clap_id id, double value, char *display, u32 size) {
    u32 param_index = (u32) id;
    
    switch (param_index) {
        case Gain: {
            snprintf(display, size, "%f", value);
            return true;
        }
        case Volume: {
            snprintf(display, size, "%f dB", value);
            return true;
        }
        case NParams:
        default: {
            return false;
        }
    }
}

bool param_convert_text_to_value(const clap_plugin_t *_plugin, clap_id param_id, const char *display, double *value) {

    u32 param_index = (u32)param_id;
    if (param_index >= NParams) { return false; }

    *value = (double)atoi(display);
    return true;
}

void param_flush(const clap_plugin_t *_plugin, const clap_input_events_t *in, const clap_output_events_t *out) {
    MyPlugin *plugin = (MyPlugin*)_plugin->plugin_data;
    const u32 event_count = in->size(in);
    
    PluginSyncMainToAudio(plugin, out);
    
    for (u32 event_index = 0; event_index < event_count; event_index++) {
        PluginProcessEvent(plugin, in->get(in, event_index));
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

bool plugin_state_save(const clap_plugin_t *_plugin, const clap_ostream_t *stream) {
    MyPlugin *plugin = (MyPlugin*)_plugin->plugin_data;
    PluginSyncAudioToMain(plugin);
    
    u32 num_params_written = stream->write(stream, plugin->main_param_values, sizeof(float)*NParams);
    
    return num_params_written == sizeof(float) * NParams;
}

bool plugin_state_load(const clap_plugin_t *_plugin, const clap_istream_t *stream) {
    MyPlugin *plugin = (MyPlugin*)_plugin->plugin_data;
    
    // not thread safe
    u32 num_params_read = stream->read(stream, plugin->main_param_values, sizeof(float)* NParams);
    bool success = num_params_read == sizeof(float)*NParams;
    for (u32 param_indedx = 0; param_indedx < NParams; param_indedx++) { 
        plugin->main_param_changed[param_indedx] = true; 
    }
    return success;
}

global_const clap_plugin_state_t extensionState = {
    .save = plugin_state_save,  
    .load = plugin_state_load,
};


// GUI

#define GUI_WIDTH 300
#define GUI_HEIGHT 200
#define GUI_API CLAP_WINDOW_API_WIN32

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

static void PluginPain(MyPlugin *plugin, u32 *bits) {

}

static void PluginProcessMouseDrag(MyPlugin *plugin, i32 x, i32 y) {

}

static void PluginProcessMousePress(MyPlugin *plugin, i32 x, i32 y) {

}

static void PluginProcessMouseRelease(MyPlugin *plugin) {

}


static void GUIPaint(MyPlugin *plugin, bool internal) {
    RedrawWindow(plugin->gui.window, 0, 0, RDW_INVALIDATE);
}

LRESULT CALLBACK GUIWindowProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    MyPlugin *plugin = (MyPlugin *) GetWindowLongPtr(window, 0);

    if (!plugin) {
        return DefWindowProc(window, message, wParam, lParam);
    }
    
    GUI *gui = &plugin->gui;
    ImGui::SetCurrentContext(gui->imgui_context);
    
    if (ImGui_ImplWin32_WndProcHandler(window, message, wParam, lParam)) {
        return true;
    }
    
    switch (message) {
        // case WM_PAINT: {
        //     PAINTSTRUCT paint;
        //     HDC dc = BeginPaint(window, &paint);
        //     BITMAPINFO info = { { sizeof(BITMAPINFOHEADER), GUI_WIDTH, -GUI_HEIGHT, 1, 32, BI_RGB } };
        //     StretchDIBits(dc, 0, 0, GUI_WIDTH, GUI_HEIGHT, 0, 0, GUI_WIDTH, GUI_HEIGHT, plugin->gui.bits, &info, DIB_RGB_COLORS, SRCCOPY);
        //     EndPaint(window, &paint);
        //     break;
        // }  
        
        // case WM_MOUSEMOVE: {
        //     PluginProcessMouseDrag(plugin, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
        //     // GUIPaint(plugin, true);
        //     break;
        // } 
        // case WM_LBUTTONDOWN: {
        //     SetCapture(window); 
        //     PluginProcessMousePress(plugin, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
        //     // GUIPaint(plugin, true);
        //     break;
        // } 
        // case WM_LBUTTONUP: {
        //     ReleaseCapture(); 
        //     PluginProcessMouseRelease(plugin);
        //     // GUIPaint(plugin, true);
        //     break;
        // }
        case WM_TIMER: {
            ImGui::SetCurrentContext(gui->imgui_context);
            wglMakeCurrent(gui->device_context, gui->opengl_context);
            
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
            
            ImGuiIO& io = ImGui::GetIO();
            
            {
                
                ImGui::Begin("Test plugin clap");
                
                ImGui::SliderFloat("value1", &gui->ui_value1, 0.0f, 1.0f);
                ImGui::SliderFloat("value2", &gui->ui_value2, 0.0f, 1.0f);
                ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.0f / io.Framerate, io.Framerate);

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

    MyPlugin* plugin = (MyPlugin*)_plugin->plugin_data;

    plugin->gui.windowClass = {};
    
    memset(&plugin->gui.windowClass, 0, sizeof(plugin->gui.windowClass));
    plugin->gui.windowClass.lpfnWndProc = GUIWindowProcedure;
    plugin->gui.windowClass.cbWndExtra = sizeof(MyPlugin *);
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
    GUIPaint(plugin, true);
    
    plugin->gui.width = GUI_WIDTH;
    plugin->gui.height = GUI_HEIGHT;    
    return true;
}

static void destroy_gui(const clap_plugin_t *_plugin) {
    MyPlugin* plugin = (MyPlugin*)_plugin->plugin_data;
    
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
    
    MyPlugin *plugin = (MyPlugin*)_plugin->plugin_data;
    
    SetParent(plugin->gui.window, (HWND)parent_window->win32);
    return true;
}

static bool set_gui_transient(const clap_plugin_t *_plugin, const clap_window_t *window) {
    return false;
}

static void suggest_gui_title(const clap_plugin_t *_plugin, const char *title) {}

static bool show_gui(const clap_plugin_t *_plugin) {
    MyPlugin *plugin =(MyPlugin*)_plugin->plugin_data;
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
    MyPlugin *plugin =(MyPlugin*)_plugin->plugin_data;
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

static void PluginSyncMainToAudio(MyPlugin *plugin, const clap_output_events_t *out) {

    // not thread safe    
    for (u32 param_index = 0; param_index < NParams; param_index++) {
        if (plugin->main_param_changed[param_index]) {
            plugin->audio_param_values[param_index] = plugin->main_param_values[param_index];
            plugin->main_param_changed[param_index] = false;
            
            clap_event_param_value_t event = {0};
            event.header.size = sizeof(event);
            event.header.time = 0;
            event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
            event.header.type = CLAP_EVENT_PARAM_VALUE;
            event.header.flags = 0;
            event.param_id = param_index;
            event.cookie = NULL;
            event.note_id = -1;
            event.port_index = -1;
            event.channel = -1;
            event.key = -1;
            event.value = plugin->audio_param_values[param_index];
            out->try_push(out, &event.header);
        }
    }
}


static bool PluginSyncAudioToMain(MyPlugin *plugin) {
    bool any_param_has_changed = false;

    // not thread safe    
    for (u32 param_index = 0; param_index < NParams; param_index++) {
        if (plugin->audio_param_changed[param_index]) {
            plugin->main_param_values[param_index] = plugin->audio_param_values[param_index];
            plugin->audio_param_changed[param_index] = false;
            any_param_has_changed = true;
        }
    }
    
    return any_param_has_changed;
}

 
static void PluginProcessEvent(MyPlugin *plugin, const clap_event_header_t *event) {
    if (event->space_id == CLAP_CORE_EVENT_SPACE_ID) {
        if (event->type == CLAP_EVENT_PARAM_VALUE) {
            const clap_event_param_value_t *param_event = (clap_event_param_value_t*)event;
            
            u32 param_index = param_event->param_id;
            
            // not thread safe
            plugin->audio_param_values[param_index]  = (float)param_event->value;
            plugin->audio_param_changed[param_index] = true;
        }
    }
}


static void PluginRenderAudio(MyPlugin *plugin, u32 start, u32 end, float **inputs, float **outputs) {
    for (u32 index = start; index < end; index++) {
        
        float in_gain    = plugin->audio_param_values[Gain];
        float out_volume = powf(10.0f, plugin->audio_param_values[Volume] * 0.05f);
        
        outputs[0][index] = out_volume * tanhf(in_gain * inputs[0][index]);
        outputs[1][index] = out_volume * tanhf(in_gain * inputs[1][index]);
    
    }
}


static clap_process_status plugin_class_process(const clap_plugin *_plugin, const clap_process_t *process) {
    MyPlugin *plugin = (MyPlugin*)_plugin->plugin_data;

    PluginSyncMainToAudio(plugin, process->out_events);
    
    assert(process->audio_outputs_count == 1);
    assert(process->audio_inputs_count == 1);
    
    const u32 frame_count = process->frames_count;
    const u32 input_event_count = process->in_events->size(process->in_events);
    
    u32 event_index = 0;
    u32 next_event_frame = input_event_count ? 0 : frame_count;
    
    for (u32 index = 0; index < frame_count;) {
        while (event_index < input_event_count && next_event_frame == index) {
            const clap_event_header_t *event = process->in_events->get(process->in_events, event_index);
            
            if (event->time != index) {
                next_event_frame = event->time;
                break;
            }
            
            PluginProcessEvent(plugin, event);
            event_index++;
            
            if (event_index == input_event_count) {
                next_event_frame = frame_count;
                break;
            }
        }
                
        PluginRenderAudio(plugin, index, next_event_frame, process->audio_inputs[0].data32, process->audio_outputs[0].data32);
        index = next_event_frame;
    }
    
    return CLAP_PROCESS_CONTINUE;
}

static bool plugin_class_init(const clap_plugin *_plugin)  {
    MyPlugin *plugin = (MyPlugin*)_plugin->plugin_data;
    for (u32 param_index = 0; param_index < NParams; param_index++) {
        clap_param_info_t information = {0};
        extensionParams.get_info(_plugin, param_index, &information);
        plugin->main_param_values[param_index] = information.default_value;
        plugin->audio_param_values[param_index] = information.default_value; 
    }
    
    return true;
}

static void plugin_class_destroy(const clap_plugin *_plugin) {
    MyPlugin *plugin = (MyPlugin*)_plugin->plugin_data;
    free(plugin);
}

static bool plugin_class_activate(const clap_plugin *_plugin, double samplerate, u32 min_buffer_size, u32 max_buffer_size) {
    MyPlugin *plugin = (MyPlugin*)_plugin->plugin_data;
    plugin->samplerate = samplerate;
    return true;
}

static void plugin_class_deactivate(const clap_plugin *_plugin) {}

static bool plugin_class_start_processing(const clap_plugin *_plugin) {
    return true;
}

static void plugin_class_stop_processing(const clap_plugin *_plugin) {}

static void plugin_class_reset(const clap_plugin *_plugin) {
    MyPlugin *plugin = (MyPlugin*)_plugin->plugin_data;
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

// Retrieves a plugin descriptor by its index.
// Returns null in case of error.
// The descriptor must not be freed.
// [thread-safe]
const clap_plugin_descriptor_t* get_plugin_descriptor(const clap_plugin_factory_t *factory, u32 index) {
    return index == 0 ? &pluginDescriptor : nullptr;
}
  
// Create a clap_plugin by its plugin_id.
// The returned pointer must be freed by calling plugin->destroy(plugin);
// The plugin is not allowed to use the host callbacks in the create method.
// Returns null in case of error.
// [thread-safe]
const clap_plugin_t* create_plugin(const clap_plugin_factory_t *factory, const clap_host_t *host, const char *pluginID) {
    if (!clap_version_is_compatible(host->clap_version) || strcmp(pluginID, pluginDescriptor.id)) {
        return nullptr;
    }
    
    MyPlugin *plugin = (MyPlugin*)calloc(1, sizeof(MyPlugin));
    plugin->host = host;
    plugin->plugin = pluginClass;
    plugin->plugin.plugin_data = plugin;
    return &plugin->plugin;
}

global_const clap_plugin_factory_t pluginFactory {
    .get_plugin_count = get_plugin_count,

    // Retrieves a plugin descriptor by its index.
    // Returns null in case of error.
    // The descriptor must not be freed.
    // [thread-safe]
    .get_plugin_descriptor = get_plugin_descriptor,
      
    // Create a clap_plugin by its plugin_id.
    // The returned pointer must be freed by calling plugin->destroy(plugin);
    // The plugin is not allowed to use the host callbacks in the create method.
    // Returns null in case of error.
    // [thread-safe]
    .create_plugin = create_plugin,
};


// plugin entry

bool lib_init(const char *path) { return true; }
void lib_deinit() {}
const void* lib_get_factory(const char *id) {
    return strcmp(id, CLAP_PLUGIN_FACTORY_ID) ? nullptr : &pluginFactory;
}
