#include "plugin.hpp"

#ifdef METAMODULE_BUILTIN
extern Plugin *pluginInstance;
#else
Plugin *pluginInstance{nullptr};
#endif

// The "init" function must be present.
// Called by Rack when it loads the plugin.
#ifdef METAMODULE_BUILTIN
void init_thereelpeetseqmm(Plugin *p) {
#else
void init(Plugin *p) {
#endif
  pluginInstance = p;

  // Register all models here so that Rack knows what to
  // load and display in the module browser.
  p->addModel(modelTheReelPeet);

  // Other plugin initialization may go here. But a better strategy is to
  // do other initialization on-demand in the module constructor.
}
