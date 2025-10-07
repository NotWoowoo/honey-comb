#include <math.h>
#include <stdio.h>
#include "common.hpp"
#include "nanovg.h"

// --- Plugin Stuff --------------------

struct Plugin {
    enum Param{
        p_delay,
        p_iters,
        p_att,
        p_smooth,

        NUM_PARAMS
    };
    static constexpr struct {
        cstr name, units;
        f64 min, max, defl; // TODO: set defl parameters on first init
        f64 (* norm)(f64);
        f64 (* denorm)(f64);
    } PARAM_INFO[] = {
        {"FREQUENCY", " hz", 0, 10000, 100,
            [](f64 v){ return sqrt(sqrt(v/10000.)); },
            [](f64 v){ return v*v*v*v*10000.; }
        },
        {"ITERATIONS", "", 0, 1000, 1,
            [](f64 v){ return sqrt(v/1000.); },
            [](f64 v){ return v*v*1000.; }
        },
        {"ATTENUATION", " %", 0, 1, 0.5, nullptr, nullptr},
        {"SMOOTHING", " %", 0, 1, 0.5, 
            [](f64 v){ return pow(v, 1000); },
            [](f64 v){ return pow(v, 0.001); }
        }
    };

    f64 params[NUM_PARAMS];

    f64 param_normed(Param p, f64 v){
        auto f = PARAM_INFO[p].norm;
        if(f) v = f(v);
        return v;
    }
    f64 param_normed(Param p){ return param_normed(p, params[p]); }
    f64 param_denormed(Param p, f64 v){
        auto f = PARAM_INFO[p].denorm;
        if(f) v = f(v);
        return v;
    }
    f64 param_denormed(Param p){ return param_denormed(p, params[p]); }
    void param_to_str(Param p, char *dst, u32 dst_max){
        cstr fmt = "%.2lf%s";
        f32 val = params[p];
        if(p == p_iters){
            fmt = "%.0lf%s";
            val = floor(val);
        }
        if(p == p_smooth) // p_smooth is unit-less and very concentrated around 1.0, so just show normed
            val = param_normed(p_smooth, val);
        snprintf(dst, dst_max, fmt, val, PARAM_INFO[p].units);
    }

    static void global_start(){}
    static void global_end(){}

    static constexpr u32 BUFF_MAX = 4*44100;
    f64 smoothed_delay;
    u32 buff_end;
    f32 buff[2][BUFF_MAX];

    void instance_start(){
        buff_end = 0;
        smoothed_delay = params[p_delay];
    }

    void instance_end(){}

    bool save(void **src, usize *size, u32 iter){
        *src = params;
        *size = sizeof(params);
        return false;
    }

    bool load(void **dst, usize *size, u32 iter){
        switch(iter){
        case 0:
            *dst = params; 
            *size = sizeof(params);
            return true;
        case 1:
            smoothed_delay = params[p_delay];
        }
        return false;
    }

    u32 calc_offset(u32 iter){
        i32 sample_offset = 44100/smoothed_delay * (iter+1);
        sample_offset = buff_end - 1 - sample_offset;
        if(sample_offset < 0)
            sample_offset += BUFF_MAX * (1 + -sample_offset/BUFF_MAX);
        sample_offset %= BUFF_MAX;
        return sample_offset;
    }

    f32 calc_att_mul(u32 iter){ return expf(-5*params[p_att]*iter/params[p_iters]); }

    void process_audio(f32 **in, f32 **out, u32 nsamples){
        for(u32 i = 0; i < nsamples; ++i){
            f64 p = params[p_smooth];
            smoothed_delay = p*smoothed_delay + (1-p)*params[p_delay];
            
            buff[0][buff_end] = in[0][i];
            buff[1][buff_end] = in[1][i];
            buff_end += 1;
            buff_end %= BUFF_MAX;

            f32 sum[2] = {0, 0}; 

            for(u32 j = 0; j < floor(params[p_iters]); ++j){
                f32 att_mul = calc_att_mul(j);
                u32 sample_offset = calc_offset(j);
                sum[0] += att_mul * buff[0][sample_offset];
                sum[1] += att_mul * buff[1][sample_offset];
            }

            out[0][i] = in[0][i] + sum[0];
            out[1][i] = in[1][i] + sum[1];
        }
    }

    // GUI

    constexpr static u32 gui_w = 400, gui_h = 450; // could be regular vars in future for resizable
    i32 logo_img;
    f64 mouse_x, mouse_y;
    f64 mouse_y_prev, mouse_dy;
    bool mouse_down;
    bool shift_down;
    static constexpr u32 NONE_ACTIVE = ~0;
    u32 active_param;

    void render_init(NVGcontext *);
    void render_deinit(NVGcontext *);
    void render(NVGcontext *);
};

static const u8 inter_regular[] = {
    #embed "res/Inter-Regular.otf"
};

static const u8 inter_semibold[] = {
    #embed "res/Inter-SemiBold.otf"
};

static const u8 logo_png[] = {
    #embed "res/logo.png"
};

#include <stdio.h>

enum class NotifyHostOf {
    PARAM_BEGIN,
    PARAM_UPDATE,
    PARAM_END
};
void notify_host(Plugin *, NotifyHostOf, u32, f64);

void Plugin::render_init(NVGcontext *vg){
    active_param = NONE_ACTIVE;
    if(nvgCreateFontMem(vg, "inter", (u8 *)inter_regular, sizeof(inter_regular), false) == -1)
        printf("Can't load font 'inter'\n");
    if(nvgCreateFontMem(vg, "inter-semibold", (u8 *)inter_semibold, sizeof(inter_semibold), false) == -1)
        printf("Can't load font 'inter-semibold'\n");
    if((logo_img = nvgCreateImageMem(vg, 0, (u8 *)logo_png, sizeof(logo_png))) == 0)
        printf("Can't load logo png\n");
}

void Plugin::render_deinit(NVGcontext *vg){}

void Plugin::render(NVGcontext *vg){
    // bg
    { 
        u8
            r = param_normed(p_delay)*10,
            g = param_normed(p_iters)*7,
            b = param_normed(p_smooth)*5;
        f64 mul = 1 - param_normed(p_att);
        r *= mul;
        g *= mul;
        b *= mul;

        
        NVGpaint bg_paint_1 = nvgLinearGradient(vg, 0, 0, gui_w/2.f, gui_h/2.f,
            nvgRGBA(58, 60, 42, 255),
            nvgRGBA(16, 21, 26, 0)
        );
        NVGpaint bg_paint_2 = nvgLinearGradient(vg, gui_w/2.f, gui_h/2.f, gui_w, gui_h,
            nvgRGBA(16, 21, 26, 0),
            nvgRGBA(45, 49, 52, 255)
        );
        
        nvgBeginPath(vg);
        nvgRect(vg, 0, 0, gui_w, gui_h);
        nvgFillColor(vg, nvgRGB(16+r, 21+g, 26+b));
        nvgFill(vg);

        nvgBeginPath(vg);
        nvgRect(vg, 0, 0, gui_w, gui_h);
        nvgFillPaint(vg, bg_paint_1);
        nvgFill(vg);

        nvgBeginPath(vg);
        nvgRect(vg, 0, 0, gui_w, gui_h);
        nvgFillPaint(vg, bg_paint_2);
        nvgFill(vg);
    }

    {
        // waveform
        f32
            y = 100,
            x = 0;
        u32 inc = 4;
        u32 stride = inc * (f32)BUFF_MAX / gui_w;
        nvgBeginPath(vg);
        nvgMoveTo(vg, x, y);
        for(u32 i = 0; i < gui_w / inc; ++i){
            f32 v = buff[0][stride*i];
            v += buff[1][stride*i];
            v /= 2.f;
            nvgLineTo(vg, x, y+v*y/2.f);
            x += inc;
        }
        nvgStrokeColor(vg, nvgRGBA(239, 202, 1, 50));
        nvgStrokeWidth(vg, 2);
        nvgStroke(vg);

        // delay taps
        nvgBeginPath(vg);
        for(u32 i = 0; i < params[p_iters]; ++i){
            f32 x = gui_w*calc_offset(i)/(f32)BUFF_MAX;
            f32 h = calc_att_mul(i);
            nvgMoveTo(vg, x, y-h*y/4.f);
            nvgLineTo(vg, x, y+h*y/4.f);
        }
        nvgStrokeColor(vg, nvgRGBA(255, 179, 71, 50));
        nvgStrokeWidth(vg, 2);
        nvgStroke(vg);
    }

    // Logo
    {
        f32
            center = gui_w / 2.f,
            y = 24,
            w = 220,
            h = w * 266/894.f,
            ox = center - w/2.f;

        NVGpaint img_paint = nvgImagePattern(vg, ox, y, w, h, 0, logo_img, 1.f);
        nvgBeginPath(vg);
        nvgRect(vg, ox, y, w, h);
        nvgFillPaint(vg, img_paint);
        nvgFill(vg);
    }

    static auto draw_hex = [](NVGcontext *vg, f32 x, f32 y, f32 r){
        nvgMoveTo(vg, x+r, y);
        for(u32 i = 1; i <= 6; ++i){
            f32 a = 2*M_PI * i/6.f;
            f32 dx = cos(a)*r;
            f32 dy = sin(a)*r;
            nvgLineTo(vg, x+dx, y+dy); 
        }
    };

    static auto draw_knob = [](Plugin *plug, NVGcontext *vg, f32 x, f32 y, f32 r, Param p){
        bool hovering = 
            plug->mouse_x >= x-r && plug->mouse_x <= x+r &&
            plug->mouse_y >= y-r && plug->mouse_y <= y+r;

        bool use_active_col = (plug->active_param == NONE_ACTIVE && hovering) || plug->active_param == p;

        // body (hexagon)
        NVGpaint body_paint = nvgLinearGradient(vg, x-r, y-r, x+r, y+r,
            nvgRGB(46, 55, 65),
            nvgRGB(20, 24, 29)
        );
        nvgBeginPath(vg);
        draw_hex(vg, x, y, r);
        // nvgFillColor(vg, active_col ? nvgRGB(150, 100, 80) : nvgRGB(100, 53, 80));
        nvgFillPaint(vg, body_paint);
        nvgFill(vg);

        // outline 1
        NVGcolor c = use_active_col ? nvgRGB(161, 139, 16) : nvgRGB(111, 103, 36);
        for(u32 i = 0; i < 4; ++i){
            f32 p = i/5.f;
            NVGcolor c2 = c;
            c2.a = 0;
            c2 = nvgLerpRGBA(c, c2, sqrt(sqrt(p)));
            nvgBeginPath(vg);
            draw_hex(vg, x, y, r*(1+i/2.f));
            nvgStrokeColor(vg, c2);
            nvgStrokeWidth(vg, 2);
            nvgStroke(vg);
        }

        // outline 2
        nvgBeginPath(vg);
        draw_hex(vg, x, y, r + 7);
        nvgStrokeColor(vg, nvgRGBA(200, 200, 200, 50));
        nvgStrokeWidth(vg, 4);
        nvgStroke(vg);

        // position
        f64 p_val = plug->param_normed(p);
        f32 a = p_val*-0.5 + (1-p_val)*(0.5+3.14159);
        f32 dx = 0.7*r*cos(a); 
        f32 dy = -0.7*r*sin(a);
        nvgBeginPath(vg);
        nvgMoveTo(vg, x, y);
        nvgLineTo(vg, x+dx, y+dy);
        // nvgStrokeColor(vg, nvgRGB(255,100,100));
        nvgStrokeColor(vg, nvgRGB(239, 202, 1));
        nvgStrokeWidth(vg, 4);
        nvgStroke(vg);

        // extra position for delay
        if(p == p_delay){    
            f64 p_val = plug->param_normed(p, plug->smoothed_delay);
            f32 a = p_val*-0.5 + (1-p_val)*(0.5+3.14159);
            f32 dx = 1.1*r*cos(a); 
            f32 dy = -1.1*r*sin(a);
            nvgBeginPath(vg);
            nvgCircle(vg, x+dx, y+dy, 3);
            // nvgStrokeColor(vg, nvgRGB(255,100,100));
            nvgFillColor(vg, nvgRGB(239, 202, 1));
            nvgFill(vg);
        }

        // value box
        nvgBeginPath(vg);
        f32 w = 40;
        nvgRoundedRect(vg, x-w, y+r+6.5, 2*w, 17, 4);
        nvgFillColor(vg, nvgRGBA(0, 0, 0, 100));
        nvgFill(vg);

        nvgBeginPath(vg);
        nvgRoundedRect(vg, x-w, y+r+6.5, 2*w, 17, 4);
        nvgStrokeColor(vg, nvgRGBA(239, 202, 1, 150));
        nvgStrokeWidth(vg, 1);
        nvgStroke(vg);

        // value
        static char value_txt[256];
        plug->param_to_str(p, value_txt, 256);
        nvgFontSize(vg, 12.f);
        nvgFontFace(vg, "inter-semibold");
        nvgTextAlign(vg, NVG_ALIGN_CENTER|NVG_ALIGN_TOP);
        nvgFillColor(vg, nvgRGB(239, 202, 1));
        nvgText(vg, x, y+r+10, value_txt, NULL); 

        // label
        nvgFontSize(vg, 12.f);
        nvgFontFace(vg, "inter-semibold");
        nvgTextAlign(vg, NVG_ALIGN_CENTER|NVG_ALIGN_BOTTOM);
        nvgFillColor(vg, nvgRGB(233, 233, 227));
        nvgText(vg, x, y-r-10, PARAM_INFO[p].name, NULL); 
        
        return hovering;
    };

    f32
        x = gui_w*0.29f,
        y = gui_h*0.337f,
        y_abs = 38,
        r = 450*0.075f;

    for(u32 p = 0; p < NUM_PARAMS; ++p){
        if(p%2 == 1) x = gui_w - x;
        if(p == 2) y = gui_h - y;
        if(draw_knob(this, vg, x, y+y_abs, r, (Param)p) && mouse_down && active_param == NONE_ACTIVE){
            active_param = p;
            notify_host(this, NotifyHostOf::PARAM_BEGIN, p, params[p]);
        }
    }

    mouse_dy = mouse_y - mouse_y_prev;
    mouse_y_prev = mouse_y;

    if(mouse_down && active_param != NONE_ACTIVE){
        Param p = (Param)active_param;
        f64 v = param_normed(p);
        f64 div = shift_down ? 800 : 200;
        v -= mouse_dy/div;
        if(v > 1) v = 1;
        if(v < 0) v = 0;
        params[p] = param_denormed(p, v);
        if(fabs(mouse_dy) > 0.1)
            notify_host(this, NotifyHostOf::PARAM_UPDATE, p, params[p]);
    }

    if(! mouse_down && active_param != NONE_ACTIVE){
        notify_host(this, NotifyHostOf::PARAM_END, active_param, params[active_param]);
        active_param = NONE_ACTIVE;
    }
}

// ---- CPLUG And Platform Stuff ----

#include "cplug.h"

#define GLAD_GL_IMPLEMENTATION
#include "glad/gl.h"
#include "GLFW/glfw3.h"

#include "nanovg.h"
#define NANOVG_GL3_IMPLEMENTATION
#include "nanovg_gl.h"

// const int CPLUG_NUM_PARAMS_VAR = Plugin::NUM_PARAMS;

static void render_fn();

#if defined _WIN32

    #include <windows.h>
    #define GLFW_EXPOSE_NATIVE_WIN32
    #include "GLFW/glfw3native.h"

    static UINT_PTR timer_id;

    static void CALLBACK TimerProc(HWND hwnd, UINT uMsg, UINT_PTR idEvent, DWORD dwTime){ render_fn(); }
    static void start_render_timer(u32 ms){
        render_fn(); // render first frame immediately
        timer_id = SetTimer(NULL, 0, ms, TimerProc);
    }
    static void end_render_timer(){
        KillTimer(NULL, timer_id);
    }

#elif defined __unix__

    #include <X11/Xlib.h>
    #include <X11/Xutil.h>
    #include <X11/Xos.h>
    #include <sys/time.h>
    #include <signal.h>
    #define GLFW_EXPOSE_NATIVE_X11
    #include "GLFW/glfw3native.h"

    static void timer_handler(int sig){ render_fn(); }
    static void start_render_timer(u32 ms){
        render_fn(); // render first frame immediately

        signal(SIGALRM, timer_handler);
        itimerval timer;
        u32 interval = ms*1000;
        timer.it_interval.tv_sec = 0;
        timer.it_interval.tv_usec = interval;
        timer.it_value.tv_sec = 0;
        timer.it_value.tv_usec = interval;
        setitimer(ITIMER_REAL, &timer, NULL);
    }
    static void end_render_timer(){
        itimerval timer = {{0, 0}, {0, 0}};
        setitimer(ITIMER_REAL, &timer, NULL);
    }

#endif

struct PluginWrapper {
    Plugin p; // Pointer to PluginWrapper must be a pointer to Plugin
    GLFWwindow *window;
    static PluginWrapper *instances;
    PluginWrapper *next;
    NVGcontext *vg;
    cplug_atomic_i32 main_to_audio_head, main_to_audio_tail;
    CplugEvent main_to_audio_queue[CPLUG_EVENT_QUEUE_SIZE];
    bool focus_click;

    void gui_start(){
        glfwWindowHint(GLFW_DECORATED, GLFW_FALSE);
        glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
        // glfwWindowHint(GLFW_FOCUSED, GLFW_FALSE);
        // // glfwWindowHint(GLFW_SAMPLES, 4);

        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

        window = glfwCreateWindow(p.gui_w, p.gui_h, "plug", NULL, NULL);
        CPLUG_LOG_ASSERT(window);

        glfwMakeContextCurrent(window);
        gladLoadGL(glfwGetProcAddress);
        glfwSwapInterval(1);

        vg = nvgCreateGL3(NVG_ANTIALIAS);

        p.render_init(vg);

        glfwSetWindowUserPointer(window, this);
        glfwSetCursorPosCallback(window, [](GLFWwindow *window, f64 x, f64 y){
            PluginWrapper *p = (PluginWrapper *)glfwGetWindowUserPointer(window);
            p->p.shift_down = glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS;
            p->p.mouse_x = x;
            p->p.mouse_y = y;
        });
        glfwSetWindowFocusCallback(window, [](GLFWwindow *window, i32 focused){
            PluginWrapper *p = (PluginWrapper *)glfwGetWindowUserPointer(window);
            p->focus_click = true;
        });
        glfwSetMouseButtonCallback(window, [](GLFWwindow *window, i32 button, i32 action, i32 mods){
            PluginWrapper *p = (PluginWrapper *)glfwGetWindowUserPointer(window);
            #ifdef __linux__
                // workaround for issue at least on X11: click to focus generates an extra release event (WHY?!?!?!)
                if(p->focus_click && action == GLFW_RELEASE){
                    p->focus_click = false;
                    return;
                }
            #endif
           p->p.mouse_down = action == GLFW_PRESS;
        });
        // glfwSetKeyCallback(window, [](GLFWwindow *window, i32 key, i32 scancode, i32 action, i32 mods){
        //     PluginWrapper *p = (PluginWrapper *)glfwGetWindowUserPointer(window);
        //     if(key == GLFW_KEY_LEFT_SHIFT){
        //         p->p.shift_down = action == GLFW_PRESS;
        //     }
        // });

        // Add to instances for timer
        bool start_timer = ! instances;
        next = instances;
        instances = this;
        if(start_timer) // start timer after instances inited
            start_render_timer(16);
    }

    void gui_end(){
        // Remove from instances for timer
        if(instances == this)
            instances = next;
        else for(PluginWrapper *pw = instances; pw; pw = pw->next){
            if(pw->next != this) continue;
            pw->next = next;
            break;
        }
        if(! instances)
            end_render_timer();

        p.render_deinit(vg);
        nvgDeleteGL3(vg);
        glfwDestroyWindow(window);
    }

    void render(){
        glfwMakeContextCurrent(window);
        glClear(GL_COLOR_BUFFER_BIT);

        i32 fb_width, win_width;
        glfwGetWindowSize(window, &win_width, nullptr);
        glfwGetFramebufferSize(window, &fb_width, nullptr);
        nvgBeginFrame(vg, p.gui_w, p.gui_h, fb_width/(f32)win_width);
        p.render(vg);
        nvgEndFrame(vg);

        glfwSwapBuffers(window);
    }

    void reparent(void *native_handle){
    // This will be called multiple times with NULL. If NULL then technically supposed to remove window from parent
    #if defined _WIN32
        HWND parent = (HWND)native_handle;
        HWND child = glfwGetWin32Window(window);
        // HWND old_parent = GetParent(child);

        // if(old_parent){
        //     SetParent(child, nullptr);
        // }

        if(parent){
            SetParent(child, parent);
            glfwShowWindow(window);
        }
    #elif defined __unix__
        Window parent = (Window)native_handle;
        Window child = (Window)glfwGetX11Window(window);
        Display *disp = glfwGetX11Display();

        // Window old_parent, root_return, *children_return;
        // u32 num_children_return;
        // CPLUG_LOG_ASSERT(XQueryTree(disp, child, &root_return, &old_parent, &children_return, &num_children_return));

        // if(old_parent){
        //     XUnmapWindow(disp, child);
        // }

        if(parent){
            XReparentWindow(disp, child, parent, 0, 0);
            XMapRaised(disp, child);
            XFlush(disp);
        }
    #endif
    }

    void get_size(u32 *w, u32 *h){
        glfwGetWindowSize(window, (i32 *)w, (i32 *)h); // u32* -> i32* should be fine if these numbers are positive and small enough
    }
};

PluginWrapper *PluginWrapper::instances = nullptr;

static void render_fn(){
    PluginWrapper *p = PluginWrapper::instances; 
    glfwPollEvents();
    while(p){
        p->render();
        p = p->next;
    }
}

void notify_host(Plugin *p, NotifyHostOf of, u32 param, f64 v){
    PluginWrapper *pw = (PluginWrapper *)p;
    i32 main_to_audio_head = cplug_atomic_load_i32(&pw->main_to_audio_head) & CPLUG_EVENT_QUEUE_MASK;
    CplugEvent *evt = &pw->main_to_audio_queue[main_to_audio_head];
    evt->parameter.idx = param;
    evt->parameter.value = p->param_normed((Plugin::Param)param, v);
    static constexpr u32 evt_types[] = {
        CPLUG_EVENT_PARAM_CHANGE_BEGIN,
        CPLUG_EVENT_PARAM_CHANGE_UPDATE,
        CPLUG_EVENT_PARAM_CHANGE_END
    };
    evt->parameter.type = evt_types[(u32)of];
    cplug_atomic_fetch_add_i32(&pw->main_to_audio_head, 1);
    cplug_atomic_fetch_and_i32(&pw->main_to_audio_head, CPLUG_EVENT_QUEUE_MASK);
}

void cplug_libraryLoad(){
    CPLUG_LOG_ASSERT(glfwInit());
    glfwSetErrorCallback([](int err, cstr desc){ printf("GLFW Error %i: %s\n", err, desc); });
    Plugin::global_start();
};

void cplug_libraryUnload(){
    glfwTerminate();
    Plugin::global_end();
};

void *cplug_createPlugin(){
    PluginWrapper *p = (PluginWrapper *)calloc(1, sizeof *p);
    p->p.instance_start();
    return p;
}

void cplug_destroyPlugin(void *p){
    PluginWrapper *plug = (PluginWrapper *)p;
    plug->p.instance_end();
    free(plug);
}

u32 cplug_getInputBusChannelCount(void *p, u32 idx){
    if(idx == 0) return 2;
    return 0;
}

u32 cplug_getOutputBusChannelCount(void *p, u32 idx){
    if(idx == 0) return 2;
    return 0;
}

cstr cplug_getInputBusName(void *p, u32 idx){
    if(idx == 0) return "Stereo Input";
    return "";
}

cstr cplug_getOutputBusName(void *p, u32 idx){
    if(idx == 0) return "Stereo Output";
    return "";
}

cstr cplug_getParameterName(void *p, u32 idx){ return Plugin::PARAM_INFO[idx].name; }

f64 cplug_getDefaultParameterValue(void *p, u32 idx){ return Plugin::PARAM_INFO[idx].defl; }

f64 cplug_getParameterValue(void *p, u32 idx){
    Plugin *plug = (Plugin *)p;
    return plug->param_normed((Plugin::Param)idx);
}

void cplug_setParameterValue(void *p, u32 idx, f64 v){
    Plugin *plug = (Plugin *)p;
    plug->params[idx] = plug->param_denormed((Plugin::Param)idx, v);
}

f64 cplug_denormaliseParameterValue(void *p, u32 idx, f64 v_normalized){
    // auto fn = Plugin::PARAM_INFO[idx].denorm;
    // if(fn) v_normalized = fn(v_normalized);
    return v_normalized;
}

f64 cplug_normaliseParameterValue(void *p, u32 idx, f64 v_denormalized){
    // auto fn = Plugin::PARAM_INFO[idx].norm;
    // if(fn) v_denormalized = fn(v_denormalized);
    return v_denormalized;
}

f64 cplug_parameterStringToValue(void *p, u32 idx, cstr str){
    return atof(str);
}

void cplug_parameterValueToString(void *p, u32 idx, char *dst, usize dst_size, f64 v){ 
    Plugin *plug = (Plugin *)p;
    plug->param_to_str((Plugin::Param)idx, dst, dst_size);
}

void cplug_getParameterRange(void *p, u32 idx, f64 *min, f64 *max){
    *min = 0; // Plugin::PARAM_INFO[idx].min;
    *max = 1; // Plugin::PARAM_INFO[idx].max;
}

u32 cplug_getParameterFlags(void *p, u32 idx){
    return CPLUG_FLAG_PARAMETER_IS_AUTOMATABLE;
}

u32 cplug_getLatencyInSamples(void *p){ return 0; }
u32 cplug_getTailInSamples(void *p){ return 0; }

void cplug_setSampleRateAndBlockSize(void *p, f64 sample_rate, u32 max_block_size){}

void cplug_process(void *p, CplugProcessContext *ctx){
    Plugin *plug = (Plugin *)p;
    PluginWrapper *pw = (PluginWrapper *)p;

    i32 head = cplug_atomic_load_i32(&pw->main_to_audio_head) & CPLUG_EVENT_QUEUE_MASK;
    i32 tail = cplug_atomic_load_i32(&pw->main_to_audio_tail);
    while(tail != head){
        CplugEvent *evt = &pw->main_to_audio_queue[tail];
        ctx->enqueueEvent(ctx, evt, 0);
        tail += 1;
        tail &= CPLUG_EVENT_QUEUE_MASK;
    }
    cplug_atomic_exchange_i32(&pw->main_to_audio_tail, tail);

    CplugEvent evt;
    u32 frame = 0;
    while(true){
        if(ctx->dequeueEvent(ctx, &evt, frame)){
        }else{
            break;
        }
        switch(evt.type){
        case CPLUG_EVENT_PARAM_CHANGE_UPDATE:
            cplug_setParameterValue(p, evt.parameter.idx, evt.parameter.value);
            break;
        case CPLUG_EVENT_PROCESS_AUDIO: {
            f32 **out = ctx->getAudioOutput(ctx, 0);
            f32 **in = ctx->getAudioInput(ctx, 0);

            constexpr u32 n_channels = 2;
            f32 *out_adj[n_channels];
            f32 *in_adj[n_channels];
            for(u32 i = 0; i < n_channels; ++i){
                out_adj[i] = out[i] + frame;
                in_adj[i] = in[i] + frame;
            }

            plug->process_audio(in_adj, out_adj, evt.processAudio.endFrame-frame);
            frame = evt.processAudio.endFrame;
        } break;
        default:
            break;
        }
    }
}

void cplug_saveState(void *p, const void *state_ctx, cplug_writeProc write_proc){
    Plugin *plug = (Plugin *)p;
    void *src;
    usize size;
    u32 iter = 0;
    bool cont = true;
    while(cont){
        src = nullptr;
        size = 0;
        cont = plug->save(&src, &size, iter++);
        if(src && size)
            write_proc(state_ctx, src, size);
    }
}

void cplug_loadState(void *p, const void *state_ctx, cplug_readProc read_proc){
    Plugin *plug = (Plugin *)p;
    void *src;
    usize size;
    u32 iter = 0;
    bool cont = true;
    while(cont){
        src = nullptr;
        size = 0;
        cont = plug->load(&src, &size, iter++);
        if(src && size)
            read_proc(state_ctx, src, size);
    }
}

void *cplug_createGUI(void *p){
    PluginWrapper *pw = (PluginWrapper *)p;
    pw->gui_start();
    return pw;
}

void cplug_destroyGUI(void *p){
    PluginWrapper *pw = (PluginWrapper *)p;
    pw->gui_end();
}

void cplug_setParent(void *p, void *native_handle){
    PluginWrapper *pw = (PluginWrapper *)p;
    pw->reparent(native_handle);
}

void cplug_setScaleFactor(void *p, f32 scale){}
void cplug_getSize(void *p, u32 *w, u32 *h){
    PluginWrapper *pw = (PluginWrapper *)p;
    pw->get_size(w, h);
}

void cplug_checkSize(void *p, u32 *w, u32 *h){}
bool cplug_setSize(void *p, u32 w, u32 h){ return false; }

// Clap-only GUI funcs
bool cplug_getResizeHints(void *p, bool *resizable_x, bool *resizable_y, bool *preserve_ratio, u32 *ratio_x, u32 *ratio_y){ return false; }
void cplug_setVisible(void *p, bool visible){}

