#include "plugin.hpp"

using namespace rack;
using namespace rack::componentlibrary;
using namespace rack::ui;

// =======================
//   MODULE DEFINITION
// =======================

struct TheReelPeet : Module {
  enum ParamId {
    BUTTON_A_PARAM,
    RAND_A_PARAM,
    LENGTH_A_PARAM,

    BUTTON_B_PARAM,
    RAND_B_PARAM,
    LENGTH_B_PARAM,

    BPM_A_PARAM,
    BPM_B_PARAM,

    PARAMS_LEN
  };
  enum InputId {
    IN_A_INPUT,
    IN_B_INPUT,

    RNDTRIG_A_INPUT,
    RNDTRIG_B_INPUT,

    RUN_A_INPUT,
    RUN_B_INPUT,

    HOLD_A_INPUT,
    HOLD_B_INPUT,

    INPUTS_LEN
  };
  enum OutputId {
    OUT_A_OUTPUT,
    TRIG_A_OUTPUT,
    OUT_B_OUTPUT,
    TRIG_B_OUTPUT,
    OUTPUTS_LEN
  };
  enum LightId { RUN_A_LIGHT, RUN_B_LIGHT, LIGHTS_LEN };

  float vizTempoA = 0.f;
  float vizTempoB = 0.f;

  // Lane A state
  bool runningA = false;
  int stepA = 0;
  float timerA = 0.f;
  float trigTimerA = 0.f;
  float seqA[16];

  // HOLD state A
  float holdTimerA = 0.f;
  float holdTimeA = 4.f;
  dsp::SchmittTrigger holdTrigA;

  // Lane B state
  bool runningB = false;
  int stepB = 0;
  float timerB = 0.f;
  float trigTimerB = 0.f;
  float seqB[16];

  // HOLD state B
  float holdTimerB = 0.f;
  float holdTimeB = 4.f;
  dsp::SchmittTrigger holdTrigB;

  int lenA = 2;
  int lenB = 2;

  dsp::SchmittTrigger onA, randA, randInA;
  dsp::SchmittTrigger onB, randB, randInB;

  TheReelPeet() {
    config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);

    // --- Lane A params
    configParam(
        BUTTON_A_PARAM, 0.f, 1.f, 0.f,
        "Lane A Run toggle. Toggles running when RUN CV is NOT patched.");
    configParam(RAND_A_PARAM, 0.f, 1.f, 0.f,
                "Lane A Randomize button. Randomizes Lane A sequence values.");
    configParam(LENGTH_A_PARAM, 2.f, 16.f, 3.f,
                "Lane A Length. Number of steps (2–16).");

    // --- Lane B params
    configParam(
        BUTTON_B_PARAM, 0.f, 1.f, 0.f,
        "Lane B Run toggle. Toggles running when RUN CV is NOT patched.");
    configParam(RAND_B_PARAM, 0.f, 1.f, 0.f,
                "Lane B Randomize button. Randomizes Lane B sequence values.");
    configParam(LENGTH_B_PARAM, 2.f, 16.f, 3.f,
                "Lane B Length. Number of steps (2–16).");

    // --- Tempo params
    configParam(BPM_A_PARAM, 20.f, 300.f, 120.f,
                "Lane A BPM. Base tempo (20–300).");
    configParam(BPM_B_PARAM, 20.f, 300.f, 120.f,
                "Lane B BPM. Base tempo (20–300).");

    // --- Tempo CV inputs (per lane)
    configInput(IN_A_INPUT, "Tempo CV A. When patched, sets Lane A BPM from "
                            "0–10V (20–300 BPM) and overrides the BPM knob.");
    configInput(IN_B_INPUT, "Tempo CV B. When patched, sets Lane B BPM from "
                            "0–10V (20–300 BPM) and overrides the BPM knob.");

    // --- Randomize trigger inputs (per lane)
    configInput(
        RNDTRIG_A_INPUT,
        "Randomize Trigger A. Rising edge randomizes Lane A sequence values.");
    configInput(
        RNDTRIG_B_INPUT,
        "Randomize Trigger B. Rising edge randomizes Lane B sequence values.");

    // --- Run CV inputs (per lane)
    configInput(RUN_A_INPUT, "Run CV A (gate). High = running, low = stopped. "
                             "Overrides Run toggle when patched.");
    configInput(RUN_B_INPUT, "Run CV B (gate). High = running, low = stopped. "
                             "Overrides Run toggle when patched.");

    // --- Hold trigger inputs (timed hold, per lane)
    configInput(HOLD_A_INPUT, "Hold Trigger A. Rising edge starts a ~4s freeze "
                              "(Lane A does not advance steps during hold).");
    configInput(HOLD_B_INPUT, "Hold Trigger B. Rising edge starts a ~4s freeze "
                              "(Lane B does not advance steps during hold).");

    // --- Outputs
    configOutput(
        OUT_A_OUTPUT,
        "Pitch CV A. Current step value (use with a quantizer if desired).");
    configOutput(TRIG_A_OUTPUT, "Trigger A. Short trigger on each step advance "
                                "(no triggers while held or stopped).");
    configOutput(
        OUT_B_OUTPUT,
        "Pitch CV B. Current step value (use with a quantizer if desired).");
    configOutput(TRIG_B_OUTPUT, "Trigger B. Short trigger on each step advance "
                                "(no triggers while held or stopped).");

    // --- Lights
    configLight(RUN_A_LIGHT, "Lane A running (on when running).");
    configLight(RUN_B_LIGHT, "Lane B running (on when running).");

    // --- Initialize sequences
    for (int i = 0; i < 16; i++) {
      seqA[i] = random::uniform() * 5.f;
      seqB[i] = random::uniform() * 5.f;
    }
  }
  void randomize(float *seq) {
    for (int i = 0; i < 16; i++)
      seq[i] = random::uniform() * 5.f;
  }
  void processLane(bool &running, int &step, float &timer, float &trigTimer,
                   float bpm, float *seq, int length, float &outCV,
                   float &outTrig, dsp::SchmittTrigger &onTrig,
                   dsp::SchmittTrigger &randTrig,
                   dsp::SchmittTrigger &randInTrig, float onVal, float randVal,
                   float randInV, bool runGateConnected, float runGateV,
                   dsp::SchmittTrigger &holdTrig, float holdInV,
                   float &holdTimer, float holdTime, const ProcessArgs &args) {

    // -------------------------
    // RUN STATE (forced or toggle)
    // -------------------------
    if (runGateConnected) {
      running = (runGateV >= 1.f);
    } else if (onTrig.process(onVal)) {
      running = !running;
    }

    // HOLD: only active when jack is patched
    if (holdTime > 0.f && holdTrig.process(holdInV)) {
      holdTimer = holdTime;
    }

    // countdown
    if (holdTimer > 0.f) {
      holdTimer -= args.sampleTime;
      if (holdTimer < 0.f)
        holdTimer = 0.f;
    }

    // -------------------------
    // RANDOMIZE (button or trig input)
    // -------------------------
    const bool doRand =
        randTrig.process(randVal) || randInTrig.process(randInV);
    if (doRand) {
      randomize(seq);
    }

    // -------------------------
    // TIMING
    // -------------------------
    bpm = clamp(bpm, 20.f, 300.f);
    const float stepTime = 60.f / bpm;

    outCV = 0.f;
    outTrig = 0.f;

    // gate pulse timer
    if (trigTimer > 0.f) {
      trigTimer -= args.sampleTime;
      if (trigTimer < 0.f)
        trigTimer = 0.f;
    }

    // -------------------------
    // SEQUENCING
    // -------------------------
    if (running) {
      // while held: freeze step/timer, keep output steady
      if (holdTimer <= 0.f) {
        timer += args.sampleTime;
        if (timer >= stepTime) {
          timer -= stepTime;
          step = (step + 1) % length;
          trigTimer = 0.01f;
        }
      }
      outCV = seq[step];
    } else {
      timer = 0.f;
      step = 0;
      trigTimer = 0.f;
      holdTimer = 0.f; // also clear any pending hold when stopped
    }

    if (trigTimer > 0.f)
      outTrig = 10.f;
  }
  void process(const ProcessArgs &args) override {
    // -------------------------
    // RANDOM TRIGGER INPUTS
    // -------------------------
    const float rndInA = inputs[RNDTRIG_A_INPUT].isConnected()
                             ? inputs[RNDTRIG_A_INPUT].getVoltage()
                             : 0.f;

    const float rndInB = inputs[RNDTRIG_B_INPUT].isConnected()
                             ? inputs[RNDTRIG_B_INPUT].getVoltage()
                             : 0.f;

    // -------------------------
    // LENGTH PARAMS (for display)
    // -------------------------
    lenA = clamp((int)std::round(params[LENGTH_A_PARAM].getValue()), 1, 16);
    lenB = clamp((int)std::round(params[LENGTH_B_PARAM].getValue()), 1, 16);

    float outA = 0.f, trigAout = 0.f;
    float outB = 0.f, trigBout = 0.f;

    // -------------------------
    // BPM BASE VALUES
    // -------------------------
    float bpmA = params[BPM_A_PARAM].getValue();
    float bpmB = params[BPM_B_PARAM].getValue();

    // -------------------------
    // RUN GATES
    // -------------------------
    const bool runAConn = inputs[RUN_A_INPUT].isConnected();
    const bool runBConn = inputs[RUN_B_INPUT].isConnected();

    const float runAV = runAConn ? inputs[RUN_A_INPUT].getVoltage() : 0.f;
    const float runBV = runBConn ? inputs[RUN_B_INPUT].getVoltage() : 0.f;

    // -------------------------
    // HOLD GATES (0V if unpatched)
    // -------------------------
    const float holdAV = inputs[HOLD_A_INPUT].isConnected()
                             ? inputs[HOLD_A_INPUT].getVoltage()
                             : 0.f;

    const float holdBV = inputs[HOLD_B_INPUT].isConnected()
                             ? inputs[HOLD_B_INPUT].getVoltage()
                             : 0.f;

    // -------------------------
    // BPM CV OVERRIDE (moves knob)
    // -------------------------
    if (inputs[IN_A_INPUT].isConnected()) {
      const float v = clamp(inputs[IN_A_INPUT].getVoltage(), 0.f, 10.f);
      bpmA = 20.f + (v / 10.f) * (300.f - 20.f);
      params[BPM_A_PARAM].setValue(bpmA);
    }

    if (inputs[IN_B_INPUT].isConnected()) {
      const float v = clamp(inputs[IN_B_INPUT].getVoltage(), 0.f, 10.f);
      bpmB = 20.f + (v / 10.f) * (300.f - 20.f);
      params[BPM_B_PARAM].setValue(bpmB);
    }

    // -------------------------
    // PROCESS LANES
    // -------------------------
    processLane(runningA, stepA, timerA, trigTimerA, bpmA, seqA, lenA, outA,
                trigAout, onA, randA, randInA,
                params[BUTTON_A_PARAM].getValue(),
                params[RAND_A_PARAM].getValue(), rndInA, runAConn, runAV,
                holdTrigA, holdAV, holdTimerA, holdTimeA, args);

    processLane(runningB, stepB, timerB, trigTimerB, bpmB, seqB, lenB, outB,
                trigBout, onB, randB, randInB,
                params[BUTTON_B_PARAM].getValue(),
                params[RAND_B_PARAM].getValue(), rndInB, runBConn, runBV,
                holdTrigB, holdBV, holdTimerB, holdTimeB, args);

    // -------------------------
    // OUTPUTS
    // -------------------------
    outputs[OUT_A_OUTPUT].setVoltage(outA);
    outputs[TRIG_A_OUTPUT].setVoltage(trigAout);
    outputs[OUT_B_OUTPUT].setVoltage(outB);
    outputs[TRIG_B_OUTPUT].setVoltage(trigBout);

    // -------------------------
    // LIGHTS
    // -------------------------
    lights[RUN_A_LIGHT].setBrightness(runningA ? 1.f : 0.f);
    lights[RUN_B_LIGHT].setBrightness(runningB ? 1.f : 0.f);
  }
};
// =======================
//   VERTICAL TEMPO METER
// =======================

struct VerticalTempoMeter : TransparentWidget {
  TheReelPeet *module = nullptr;
  bool isB = false;

  void draw(const DrawArgs &args) override {
    if (!module)
      return;

    float level = isB ? module->vizTempoB : module->vizTempoA;
    level = clamp(level, 0.f, 1.f);

    NVGcontext *vg = args.vg;
    Vec size = box.size;

    nvgBeginPath(vg);
    nvgRoundedRect(vg, 0, 0, size.x, size.y, 2);
    nvgFillColor(vg, nvgRGBA(0, 0, 0, 70));
    nvgFill(vg);

    float filledH = size.y * level;
    float y = size.y - filledH;

    if (filledH > 1.f) {
      nvgBeginPath(vg);
      nvgRoundedRect(vg, 1, y + 1, size.x - 2, filledH - 2, 1.5);

      NVGpaint grad =
          nvgLinearGradient(vg, 0, y, 0, size.y, nvgRGBA(80, 220, 255, 255),
                            nvgRGBA(0, 90, 190, 255));
      nvgFillPaint(vg, grad);
      nvgFill(vg);
    }

    nvgBeginPath(vg);
    nvgRoundedRect(vg, 0.5, 0.5, size.x - 1, size.y - 1, 2);
    nvgStrokeColor(vg, nvgRGBA(200, 255, 255, 160));
    nvgStrokeWidth(vg, 1);
    nvgStroke(vg);
  }
};

struct LengthDisplay : TransparentWidget {
  int *value = nullptr;

  void draw(const DrawArgs &args) override {
    if (!value)
      return;

    auto font = APP->window->loadFont(asset::system("res/fonts/ShareTechMono-Regular.ttf"));
    if (!font || font->handle < 0)
      return;

    nvgFontFaceId(args.vg, font->handle);
    nvgFillColor(args.vg, nvgRGB(0x00, 0x00, 0x00));
    nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);

    char buf[4];
    snprintf(buf, sizeof(buf), "%d", *value);

    nvgFontSize(args.vg, 10.f);
    nvgText(args.vg, box.size.x * 0.5f, box.size.y * 0.35f, buf, nullptr);

    nvgFontSize(args.vg, 9.f);
    nvgText(args.vg, box.size.x * 0.5f, box.size.y * 0.65f, "Steps", nullptr);
  }
};

struct BPMDisplay : TransparentWidget {
  Param *param = nullptr;

  void draw(const DrawArgs &args) override {
    if (!param)
      return;

    auto font = APP->window->loadFont(asset::system("res/fonts/ShareTechMono-Regular.ttf"));
    if (!font || font->handle < 0)
      return;

    int bpm = (int)std::round(param->getValue());

    nvgFontFaceId(args.vg, font->handle);
    nvgFillColor(args.vg, nvgRGB(0x00, 0x00, 0x00));
    nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);

    char buf[8];
    snprintf(buf, sizeof(buf), "%d", bpm);

    nvgFontSize(args.vg, 9.f);
    nvgText(args.vg, box.size.x * 0.5f, box.size.y * 0.35f, buf, nullptr);

    nvgFontSize(args.vg, 8.f);
    nvgText(args.vg, box.size.x * 0.5f, box.size.y * 0.65f, "BPM", nullptr);
  }
};

struct LengthParamDisplay : TransparentWidget {
  Module *module = nullptr;
  int paramId = -1;

  void draw(const DrawArgs &args) override {
    if (!module)
      return;

    int v = (int)std::round(module->params[paramId].getValue());

    nvgFontSize(args.vg, 11.f);
    nvgFontFaceId(args.vg, APP->window->uiFont->handle);
    nvgFillColor(args.vg, nvgRGB(0xcc, 0x33, 0x33));
    nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);

    char buf[4];
    snprintf(buf, sizeof(buf), "%d", v);
    nvgText(args.vg, box.size.x * 0.5f, box.size.y * 0.5f, buf, nullptr);
  }
};
struct LockOnCableKnob : RoundBlackKnob {
  TheReelPeet *m = nullptr;
  int inputId = -1;

  bool locked() const {
    return (m && inputId >= 0 && m->inputs[inputId].isConnected());
  }

  // void onChange(const event::Change& e) override {
  //     if (locked())
  //         return; // ignore user edits while CV is plugged
  //     RoundBlackKnob::onChange(e);
  // }
};

// =======================
//   WIDGET LAYOUT
// =======================

struct TheReelPeetWidget : ModuleWidget {
  TheReelPeetWidget(TheReelPeet *module) {
    setModule(module);
    setPanel(createPanel(asset::plugin(pluginInstance, "res/TheReelPeet.svg")));

    addChild(createWidget<ThemedScrew>(Vec(RACK_GRID_WIDTH, 0)));
    addChild(
        createWidget<ThemedScrew>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
    addChild(createWidget<ThemedScrew>(
        Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
    addChild(createWidget<ThemedScrew>(Vec(
        box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

    const float colA = 21.f;
    const float colB = 41.f;

    const float onY = 20.f;
    const float randY = 32.f;
    const float knobY = 46.f;
    const float bpmKnobY = 66.f;

    // const float meterY = 66.f;
    const float inY = 83.f;
    const float outY = 105.f;

    const float outDX = 4.5f;
    const float laneAX = colA - 7.f;
    const float laneBX = colB - 4.5f;

    addParam(createParamCentered<LEDButton>(mm2px(Vec(colA - 7.f, onY)), module,
                                            TheReelPeet::BUTTON_A_PARAM));
    addChild(createLightCentered<MediumLight<GreenLight>>(
        mm2px(Vec(colA - 7.f, onY)), module, TheReelPeet::RUN_A_LIGHT));

    addParam(createParamCentered<LEDButton>(
        mm2px(Vec(colB - 4.5f, onY)), module, TheReelPeet::BUTTON_B_PARAM));
    addChild(createLightCentered<MediumLight<GreenLight>>(
        mm2px(Vec(colB - 4.5f, onY)), module, TheReelPeet::RUN_B_LIGHT));

    addParam(createParamCentered<TL1105>(mm2px(Vec(colA - 7.f, randY)), module,
                                         TheReelPeet::RAND_A_PARAM));
    addParam(createParamCentered<TL1105>(mm2px(Vec(colB - 4.5f, randY)), module,
                                         TheReelPeet::RAND_B_PARAM));

    addParam(createParamCentered<RoundBlackKnob>(
        mm2px(Vec(colA - 7.f, knobY)), module, TheReelPeet::LENGTH_A_PARAM));
    addParam(createParamCentered<RoundBlackKnob>(
        mm2px(Vec(colB - 4.5f, knobY)), module, TheReelPeet::LENGTH_B_PARAM));
    auto *kbA = createParamCentered<LockOnCableKnob>(
        mm2px(Vec(laneAX, bpmKnobY)), module, TheReelPeet::BPM_A_PARAM);
    kbA->m = module;
    kbA->inputId = TheReelPeet::IN_A_INPUT;
    addParam(kbA);

    auto *kbB = createParamCentered<LockOnCableKnob>(
        mm2px(Vec(laneBX, bpmKnobY)), module, TheReelPeet::BPM_B_PARAM);
    kbB->m = module;
    kbB->inputId = TheReelPeet::IN_B_INPUT;
    addParam(kbB);

    // --- Shared lane geometry ---
    const float laneACenterX = laneAX;
    const float laneBCenterX = laneBX;

    const float cvXOffset = outDX; // left/right split for the two output jacks
    const float rndTrigY =
        outY + 11.f; // random trigger input row (under the 1v/O input)

    // ---------------------------
    // Lane A
    // ---------------------------

    // tempo CV input (top input for lane)
    addInput(createInputCentered<PJ301MPort>(mm2px(Vec(laneACenterX, inY)),
                                             module, TheReelPeet::IN_A_INPUT));

    // outputs: CV + Gate (split left/right)
    addOutput(createOutputCentered<PJ301MPort>(
        mm2px(Vec(laneACenterX - cvXOffset, outY)), module,
        TheReelPeet::OUT_A_OUTPUT));

    addOutput(createOutputCentered<PJ301MPort>(
        mm2px(Vec(laneACenterX + cvXOffset, outY)), module,
        TheReelPeet::TRIG_A_OUTPUT));

    // random trigger input (centered, under 1v/O input)
    addInput(createInputCentered<PJ301MPort>(
        mm2px(Vec(laneACenterX - cvXOffset, rndTrigY)), module,
        TheReelPeet::RNDTRIG_A_INPUT));

    // ---------------------------
    // Lane B
    // ---------------------------

    // tempo CV input
    addInput(createInputCentered<PJ301MPort>(mm2px(Vec(laneBCenterX, inY)),
                                             module, TheReelPeet::IN_B_INPUT));

    // outputs: CV + Gate
    addOutput(createOutputCentered<PJ301MPort>(
        mm2px(Vec(laneBCenterX - cvXOffset, outY)), module,
        TheReelPeet::OUT_B_OUTPUT));

    addOutput(createOutputCentered<PJ301MPort>(
        mm2px(Vec(laneBCenterX + cvXOffset, outY)), module,
        TheReelPeet::TRIG_B_OUTPUT));

    // random trigger input
    addInput(createInputCentered<PJ301MPort>(
        mm2px(Vec(laneBCenterX - cvXOffset, rndTrigY)), module,
        TheReelPeet::RNDTRIG_B_INPUT));

    if (module) {
      const float dispW = 12.f;
      // const float dispH = 8.f;
      const float labelY =
          50.f; // <-- set this to where the old "Len" label was

      auto *lenADisplay = new LengthDisplay;
      lenADisplay->box.pos = mm2px(Vec(laneAX - dispW * 0.5f, labelY));
      lenADisplay->box.size = mm2px(Vec(12.f, 11.f));
      lenADisplay->value = &module->lenA;
      addChild(lenADisplay);

      auto *lenBDisplay = new LengthDisplay;
      lenBDisplay->box.pos = mm2px(Vec(laneBX - dispW * 0.5f, labelY));
      lenBDisplay->box.size = mm2px(Vec(12.f, 11.f));
      lenBDisplay->value = &module->lenB;
      addChild(lenBDisplay);
    }

    if (module) {
      auto *bpmDispA = new BPMDisplay();
      bpmDispA->param = &module->params[TheReelPeet::BPM_A_PARAM];
      bpmDispA->box.pos = mm2px(Vec(laneAX - 6.f, bpmKnobY + 4.f));
      bpmDispA->box.size = mm2px(Vec(12.f, 11.f));
      addChild(bpmDispA);

      auto *bpmDispB = new BPMDisplay();
      bpmDispB->param = &module->params[TheReelPeet::BPM_B_PARAM];
      bpmDispB->box.pos = mm2px(Vec(laneBX - 6.f, bpmKnobY + 4.f));
      bpmDispB->box.size = mm2px(Vec(12.f, 11.f));
      addChild(bpmDispB);
    }

    // Lane A: run gate input (to the right of RND input)
    addInput(createInputCentered<PJ301MPort>(
        mm2px(Vec(laneACenterX + cvXOffset, rndTrigY)), module,
        TheReelPeet::RUN_A_INPUT));

    // Lane B: run gate input
    addInput(createInputCentered<PJ301MPort>(
        mm2px(Vec(laneBCenterX + cvXOffset, rndTrigY)), module,
        TheReelPeet::RUN_B_INPUT));

    // Lane A: HOLD input (under BPM CV)
    addInput(
        createInputCentered<PJ301MPort>(mm2px(Vec(laneACenterX, inY + 10.f)),
                                        module, TheReelPeet::HOLD_A_INPUT));

    // Lane B: HOLD input
    addInput(
        createInputCentered<PJ301MPort>(mm2px(Vec(laneBCenterX, inY + 10.f)),
                                        module, TheReelPeet::HOLD_B_INPUT));
  }
};

Model *modelTheReelPeet =
    createModel<TheReelPeet, TheReelPeetWidget>("thereelpeet-seq-mm");
