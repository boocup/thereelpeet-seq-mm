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

    DYNAMICS_A_PARAM,
    DYNAMICS_B_PARAM,

    PARAMS_LEN
  };
  enum InputId {
    RNDTRIG_A_INPUT,
    RNDTRIG_B_INPUT,

    RUN_A_INPUT,
    RUN_B_INPUT,

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

  // Lane A state
  bool runningA = false;
  int stepA = 0;
  float timerA = 0.f;
  float trigTimerA = 0.f;
  float seqA[16];
  bool gateHeldA = false;
  bool stepMutedA = false;

  // Lane B state
  bool runningB = false;
  int stepB = 0;
  float timerB = 0.f;
  float trigTimerB = 0.f;
  float seqB[16];
  bool gateHeldB = false;
  bool stepMutedB = false;

  int lenA = 2;
  int lenB = 2;

  dsp::SchmittTrigger onA, randA, randInA;
  dsp::SchmittTrigger onB, randB, randInB;

  TheReelPeet() {
    config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);

    configParam(BUTTON_A_PARAM, 0.f, 1.f, 0.f, "Lane A Run toggle");
    configParam(RAND_A_PARAM, 0.f, 1.f, 0.f, "Lane A Randomize");
    configParam(LENGTH_A_PARAM, 2.f, 16.f, 3.f, "Lane A Length (2-16 steps)");

    configParam(BUTTON_B_PARAM, 0.f, 1.f, 0.f, "Lane B Run toggle");
    configParam(RAND_B_PARAM, 0.f, 1.f, 0.f, "Lane B Randomize");
    configParam(LENGTH_B_PARAM, 2.f, 16.f, 3.f, "Lane B Length (2-16 steps)");

    configParam(BPM_A_PARAM, 20.f, 300.f, 120.f, "Lane A BPM", " BPM");
    configParam(BPM_B_PARAM, 20.f, 300.f, 120.f, "Lane B BPM", " BPM");

    configParam(DYNAMICS_A_PARAM, -1.f, 1.f, 0.f,
                "Lane A Dynamics. CCW: held gates, CW: note drops");
    configParam(DYNAMICS_B_PARAM, -1.f, 1.f, 0.f,
                "Lane B Dynamics. CCW: held gates, CW: note drops");

    configInput(RNDTRIG_A_INPUT, "Randomize Trigger A (rising edge)");
    configInput(RNDTRIG_B_INPUT, "Randomize Trigger B (rising edge)");
    configInput(RUN_A_INPUT, "Run CV A (gate: high=run, low=stop)");
    configInput(RUN_B_INPUT, "Run CV B (gate: high=run, low=stop)");

    configOutput(OUT_A_OUTPUT, "Pitch CV A (1V/Oct)");
    configOutput(TRIG_A_OUTPUT, "Gate A");
    configOutput(OUT_B_OUTPUT, "Pitch CV B (1V/Oct)");
    configOutput(TRIG_B_OUTPUT, "Gate B");

    configLight(RUN_A_LIGHT, "Lane A running");
    configLight(RUN_B_LIGHT, "Lane B running");

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
                   float &outTrig, bool &gateHeld, bool &stepMuted,
                   dsp::SchmittTrigger &onTrig, dsp::SchmittTrigger &randTrig,
                   dsp::SchmittTrigger &randInTrig, float onVal, float randVal,
                   float randInV, bool runGateConnected, float runGateV,
                   float dynamics, const ProcessArgs &args) {

    // RUN STATE
    if (runGateConnected) {
      running = (runGateV >= 1.f);
    } else if (onTrig.process(onVal)) {
      running = !running;
    }

    // RANDOMIZE
    if (randTrig.process(randVal) || randInTrig.process(randInV))
      randomize(seq);

    // TIMING
    bpm = clamp(bpm, 20.f, 300.f);
    const float stepTime = 60.f / bpm;

    outCV = 0.f;
    outTrig = 0.f;

    if (trigTimer > 0.f) {
      trigTimer -= args.sampleTime;
      if (trigTimer < 0.f)
        trigTimer = 0.f;
    }

    if (running) {
      timer += args.sampleTime;
      if (timer >= stepTime) {
        timer -= stepTime;
        step = (step + 1) % length;

        // Reset per-step state
        gateHeld = false;
        stepMuted = false;
        trigTimer = 0.f;

        const float dynVal = clamp(dynamics, -1.f, 1.f);
        if (dynVal < 0.f && random::uniform() < -dynVal) {
          gateHeld = true;
        } else if (dynVal > 0.f && random::uniform() < dynVal) {
          stepMuted = true;
        } else {
          trigTimer = 0.01f;
        }
      }

      outCV = stepMuted ? 0.f : seq[step];
      outTrig = (trigTimer > 0.f || gateHeld) ? 10.f : 0.f;
    } else {
      timer = 0.f;
      step = 0;
      trigTimer = 0.f;
      gateHeld = false;
      stepMuted = false;
    }
  }

  void process(const ProcessArgs &args) override {
    const float rndInA = inputs[RNDTRIG_A_INPUT].isConnected()
                             ? inputs[RNDTRIG_A_INPUT].getVoltage()
                             : 0.f;
    const float rndInB = inputs[RNDTRIG_B_INPUT].isConnected()
                             ? inputs[RNDTRIG_B_INPUT].getVoltage()
                             : 0.f;

    lenA = clamp((int)std::round(params[LENGTH_A_PARAM].getValue()), 1, 16);
    lenB = clamp((int)std::round(params[LENGTH_B_PARAM].getValue()), 1, 16);

    float outA = 0.f, trigAout = 0.f;
    float outB = 0.f, trigBout = 0.f;

    const bool runAConn = inputs[RUN_A_INPUT].isConnected();
    const bool runBConn = inputs[RUN_B_INPUT].isConnected();
    const float runAV = runAConn ? inputs[RUN_A_INPUT].getVoltage() : 0.f;
    const float runBV = runBConn ? inputs[RUN_B_INPUT].getVoltage() : 0.f;

    processLane(runningA, stepA, timerA, trigTimerA,
                params[BPM_A_PARAM].getValue(), seqA, lenA,
                outA, trigAout, gateHeldA, stepMutedA,
                onA, randA, randInA,
                params[BUTTON_A_PARAM].getValue(),
                params[RAND_A_PARAM].getValue(), rndInA,
                runAConn, runAV,
                params[DYNAMICS_A_PARAM].getValue(), args);

    processLane(runningB, stepB, timerB, trigTimerB,
                params[BPM_B_PARAM].getValue(), seqB, lenB,
                outB, trigBout, gateHeldB, stepMutedB,
                onB, randB, randInB,
                params[BUTTON_B_PARAM].getValue(),
                params[RAND_B_PARAM].getValue(), rndInB,
                runBConn, runBV,
                params[DYNAMICS_B_PARAM].getValue(), args);

    outputs[OUT_A_OUTPUT].setVoltage(outA);
    outputs[TRIG_A_OUTPUT].setVoltage(trigAout);
    outputs[OUT_B_OUTPUT].setVoltage(outB);
    outputs[TRIG_B_OUTPUT].setVoltage(trigBout);

    lights[RUN_A_LIGHT].setBrightness(runningA ? 1.f : 0.f);
    lights[RUN_B_LIGHT].setBrightness(runningB ? 1.f : 0.f);
  }
};

// =======================
//   DISPLAY WIDGETS
// =======================

struct LengthDisplay : TransparentWidget {
  int *value = nullptr;

  void draw(const DrawArgs &args) override {
    if (!value)
      return;

    nvgFontFace(args.vg, "sans");
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

    int bpm = (int)std::round(param->getValue());

    nvgFontFace(args.vg, "sans");
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

// =======================
//   WIDGET LAYOUT
// =======================

struct TheReelPeetWidget : ModuleWidget {
  TheReelPeetWidget(TheReelPeet *module) {
    setModule(module);
    setPanel(createPanel(asset::plugin(pluginInstance, "res/TheReelPeet.svg")));

    addChild(createWidget<ThemedScrew>(Vec(RACK_GRID_WIDTH, 0)));
    addChild(createWidget<ThemedScrew>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
    addChild(createWidget<ThemedScrew>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
    addChild(createWidget<ThemedScrew>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

    const float laneAX = 14.f;
    const float laneBX = 36.5f;

    const float onY      = 20.f;
    const float randY    = 32.f;
    const float knobY    = 46.f;
    const float bpmKnobY = 70.f;
    const float dynKnobY = 93.f;
    const float outY     = 108.f;
    const float rndTrigY = 120.f;
    const float cvDX     = 4.5f;

    // --- Lane A ---
    addParam(createParamCentered<LEDButton>(mm2px(Vec(laneAX, onY)), module, TheReelPeet::BUTTON_A_PARAM));
    addChild(createLightCentered<MediumLight<GreenLight>>(mm2px(Vec(laneAX, onY)), module, TheReelPeet::RUN_A_LIGHT));

    addParam(createParamCentered<TL1105>(mm2px(Vec(laneAX, randY)), module, TheReelPeet::RAND_A_PARAM));

    addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(laneAX, knobY)), module, TheReelPeet::LENGTH_A_PARAM));
    addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(laneAX, bpmKnobY)), module, TheReelPeet::BPM_A_PARAM));
    addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(laneAX, dynKnobY)), module, TheReelPeet::DYNAMICS_A_PARAM));

    addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(laneAX - cvDX, outY)), module, TheReelPeet::OUT_A_OUTPUT));
    addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(laneAX + cvDX, outY)), module, TheReelPeet::TRIG_A_OUTPUT));

    addInput(createInputCentered<PJ301MPort>(mm2px(Vec(laneAX - cvDX, rndTrigY)), module, TheReelPeet::RNDTRIG_A_INPUT));
    addInput(createInputCentered<PJ301MPort>(mm2px(Vec(laneAX + cvDX, rndTrigY)), module, TheReelPeet::RUN_A_INPUT));

    // --- Lane B ---
    addParam(createParamCentered<LEDButton>(mm2px(Vec(laneBX, onY)), module, TheReelPeet::BUTTON_B_PARAM));
    addChild(createLightCentered<MediumLight<GreenLight>>(mm2px(Vec(laneBX, onY)), module, TheReelPeet::RUN_B_LIGHT));

    addParam(createParamCentered<TL1105>(mm2px(Vec(laneBX, randY)), module, TheReelPeet::RAND_B_PARAM));

    addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(laneBX, knobY)), module, TheReelPeet::LENGTH_B_PARAM));
    addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(laneBX, bpmKnobY)), module, TheReelPeet::BPM_B_PARAM));
    addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(laneBX, dynKnobY)), module, TheReelPeet::DYNAMICS_B_PARAM));

    addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(laneBX - cvDX, outY)), module, TheReelPeet::OUT_B_OUTPUT));
    addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(laneBX + cvDX, outY)), module, TheReelPeet::TRIG_B_OUTPUT));

    addInput(createInputCentered<PJ301MPort>(mm2px(Vec(laneBX - cvDX, rndTrigY)), module, TheReelPeet::RNDTRIG_B_INPUT));
    addInput(createInputCentered<PJ301MPort>(mm2px(Vec(laneBX + cvDX, rndTrigY)), module, TheReelPeet::RUN_B_INPUT));

    // --- Displays ---
    if (module) {
      const float dispW = 12.f;
      // gap between LENGTH knob (46mm) and BPM knob (70mm): center at 58mm
      const float stepsDispY = 52.f;
      // gap between BPM knob (70mm) and DYN knob (93mm): center at 81.5mm
      const float bpmDispY = 76.f;

      auto *lenADisplay = new LengthDisplay;
      lenADisplay->box.pos = mm2px(Vec(laneAX - dispW * 0.5f, stepsDispY));
      lenADisplay->box.size = mm2px(Vec(dispW, 11.f));
      lenADisplay->value = &module->lenA;
      addChild(lenADisplay);

      auto *lenBDisplay = new LengthDisplay;
      lenBDisplay->box.pos = mm2px(Vec(laneBX - dispW * 0.5f, stepsDispY));
      lenBDisplay->box.size = mm2px(Vec(dispW, 11.f));
      lenBDisplay->value = &module->lenB;
      addChild(lenBDisplay);

      auto *bpmDispA = new BPMDisplay();
      bpmDispA->param = &module->params[TheReelPeet::BPM_A_PARAM];
      bpmDispA->box.pos = mm2px(Vec(laneAX - dispW * 0.5f, bpmDispY));
      bpmDispA->box.size = mm2px(Vec(dispW, 11.f));
      addChild(bpmDispA);

      auto *bpmDispB = new BPMDisplay();
      bpmDispB->param = &module->params[TheReelPeet::BPM_B_PARAM];
      bpmDispB->box.pos = mm2px(Vec(laneBX - dispW * 0.5f, bpmDispY));
      bpmDispB->box.size = mm2px(Vec(dispW, 11.f));
      addChild(bpmDispB);
    }
  }
};

Model *modelTheReelPeet =
    createModel<TheReelPeet, TheReelPeetWidget>("thereelpeet-seq-mm");
