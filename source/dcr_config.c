#include "config.h"
#include "rt_cfg.h"
#include "tasm2.h"
float tasm2_deadzone = 0.18f;
int tasm2_swap_ab;
float tasm2_render_scale = 0.80f;
static const CfgOpt opts[] = {
    {"display", "resolution", "720", "1280 x 720 landscape.", CFG_CHOICE, "720", NULL},
    {"performance", "render_scale", "0.80", "Native render-target scale: 0.80 reduces pixel area by 36%.\n# 1.00 restores full internal resolution; presentation stays 1280x720.",
     CFG_FLOAT, NULL, &tasm2_render_scale, 0.75f, 1.0f},
    CFG_ROW_BOOST(CFG_HELP_BOOST, NULL),
    CFG_ROW_SWAP_AB("Swap the gamepad's bottom and right face buttons.", &tasm2_swap_ab),
    {"controls", "deadzone", "0.18", "Analog stick deadzone.", CFG_FLOAT, NULL, &tasm2_deadzone,
     0.05f, 0.4f},
    CFG_ROW_GL_SELFTEST(NULL),
    CFG_ROW_BOOT_LOG(CFG_HELP_BOOT_LOG, NULL),
    CFG_ROW_LOG_JNI(CFG_HELP_LOG_JNI, NULL)};
static const CfgTable table = {.opts = opts, .nopts = CFG_COUNT(opts), .version = 1};
void dcr_config_load(void) { rt_config_load(&table); }
