#include <obs-module.h>
#include <stdio.h>
#include <stdlib.h>
#ifdef _WIN32
#include <windows.h>
#endif
#include <obs-frontend-api.h>

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("custom-pdf", "en-US")

const char *obs_module_description(void)
{
    return "PDF Auto Capture Plugin";
}

// ---------- PROPERTIES ----------
static obs_properties_t *get_properties(void *data)
{
    UNUSED_PARAMETER(data);

    obs_properties_t *props = obs_properties_create();

    obs_properties_add_path(
        props,
        "file",
        "Select PDF",
        OBS_PATH_FILE,
        "*.pdf",
        NULL
    );

    return props;
}

// ---------- NAME ----------
static const char *pdf_name(void *unused)
{
    UNUSED_PARAMETER(unused);
    return "PDF (Auto Stream)";
}

// ---------- CREATE ----------
static void *pdf_create(obs_data_t *settings, obs_source_t *source)
{
    UNUSED_PARAMETER(source);

    const char *file = obs_data_get_string(settings, "file");

    if (!file || !*file)
        return NULL;

    // ✅ Open PDF in default browser
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "start \"\" \"%s\"", file);
    system(cmd);

    // ✅ Give time for window to open
    Sleep(2000);

    // ✅ Create Window Capture automatically
    obs_data_t *wc_settings = obs_data_create();

    // "Chrome" or "Edge" window title guess
    obs_data_set_string(wc_settings, "window", "Chrome");

    obs_source_t *wc = obs_source_create(
        "window_capture",
        "PDF Window",
        wc_settings,
        NULL
    );

    obs_data_release(wc_settings);

    // ✅ Add to scene
    obs_source_t *scene_source = obs_frontend_get_current_scene();
    if (scene_source) {
        obs_scene_t *scene = obs_scene_from_source(scene_source);
        obs_scene_add(scene, wc);
        obs_source_release(scene_source);
    }

    return wc;
}

// ---------- LOAD ----------
bool obs_module_load(void)
{
    blog(LOG_INFO, "✅ PDF AUTO STREAM PLUGIN LOADED ✅");

    struct obs_source_info pdf = {0};

    pdf.id = "pdf_auto_stream";
    pdf.type = OBS_SOURCE_TYPE_INPUT;
    pdf.output_flags = OBS_SOURCE_VIDEO;
    pdf.get_name = pdf_name;
    pdf.create = pdf_create;
    pdf.get_properties = get_properties;

    obs_register_source(&pdf);

    return true;
}