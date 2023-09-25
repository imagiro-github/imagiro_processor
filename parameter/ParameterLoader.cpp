//
// Created by August Pemberton on 16/11/2022.
//

#include "ParameterLoader.h"

namespace imagiro {
    ParameterLoader::ParameterLoader(Processor& processor, const juce::String& yamlString, int streams, int mods)
            : processor(processor) {
        auto params = YAML::Load(yamlString.toStdString());
        load(params, streams, mods);
    }

    ParameterLoader::ParameterLoader(Processor& processor, const juce::File& yamlFile, int streams, int mods)
            : processor(processor) {
        auto params = YAML::Load(yamlFile.loadFileAsString().toStdString());
        load(params, streams, mods);
    }

    void ParameterLoader::load(const YAML::Node& config, int streams, int mods) {
        for (const auto& kv : config) {
            juce::String uid (kv.first.as<std::string>());

            if (uid == "stream") {
                for (auto i=0; i<streams; i++) {
                    auto UIDSuffix = "-" + juce::String(i);
                    auto namePrefix= "stream " + juce::String(i+1) + " ";

                    for (auto streamParam : kv.second) {
                        juce::String u = streamParam.first.as<std::string>();
                        processor.addParam(
                                loadParameter(u + UIDSuffix, streamParam.second, namePrefix, i));
                    }
                }
            }

            else if (uid == "mod") {
                for (auto i=0; i<mods; i++) {
                    auto UIDSuffix = "-" + juce::String(i);
                    auto namePrefix = "mod " + juce::String(i) + " ";

                    for (auto modParam : kv.second) {
                        juce::String u = modParam.first.as<std::string>();
                        processor.addParam(
                                loadParameter(u + UIDSuffix, modParam.second, namePrefix, i));
                    }
                }
            }

            else {
                processor.addParam(loadParameter(uid, kv.second));
            }
        }
    }

    static juce::String str (const YAML::Node& n) {
        return {n.as<std::string>()};
    }

    static float flt (const YAML::Node& n) {
        return n.as<float>();
    }

    static juce::String getString(const YAML::Node& n, const juce::String& key, juce::String defaultValue) {
        if (!n) return defaultValue;
        auto k = key.toStdString();
        return (n[k] ? str(n[k]) : defaultValue);
    }

    static float getFloat(const YAML::Node& n, const juce::String& key, float defaultValue) {
        if (!n) return defaultValue;
        auto k = key.toStdString();
        return (n[k] ? n[k].as<float>() : defaultValue);
    }

    static bool getBool(const YAML::Node& n, const juce::String& key, bool defaultValue) {
        if (!n) return defaultValue;
        auto k = key.toStdString();
        return (n[k] ? n[k].as<bool>() : defaultValue);
    }

    juce::NormalisableRange<float> ParameterLoader::getScaleRange(float min, float max, juce::String scaleID) {
        auto p = &processor;
        return {
                min, max,
                [&](float start, float end, float val) -> float {
                    return val * (end - start) + start;
                },
                [&](float start, float end, float val) -> float {
                    return (val - start) / (end - start);
                },
                [&, p, scaleID](float start, float end, float val) -> float {
                    auto snapped = p->getScale(scaleID)->getQuantized(val);
                    return snapped;
                }
        };
    }

    juce::NormalisableRange<float> ParameterLoader::getRange(juce::String parameterID, YAML::Node n) {
        auto type = getString(n, "type", "normal");

        auto min = getFloat(n, "min", 0);
        auto max = getFloat(n, "max", 1);
        auto step = getFloat(n, "step", 0);
        auto skew = getFloat(n, "skew", 1);
        auto inverse = getBool(n, "inverse", false);
        auto symmetricSkew = getBool(n, "symmetricSkew", false);

        if (type == "exp")
            return getNormalisableRangeExp(min, max, step);

        if (type == "freq")
            return getNormalisableRangeExp(20, 20000);

        if (type == "sync")
            return getTempoSyncRange(min, max, inverse);

        if (type == "scale") {
            processor.setScale(parameterID, Scale({0}).getState());
            return getScaleRange(min, max, parameterID);
        }

        return {min, max, step,
                skew, symmetricSkew};
    }


    ParameterConfig ParameterLoader::loadConfig(juce::String parameterID, juce::String configName, YAML::Node p, int index) {
        auto type = getString(p, "type", "number");
        auto range = getRange(parameterID, p["range"]);
        auto reverse = getBool(p, "reverse", false);

        float defaultVal = 0;
        if (p["default"].Type() == YAML::NodeType::Scalar)
            defaultVal = getFloat(p, "default", 0);
        else if (p["default"].Type() == YAML::NodeType::Sequence) {
            auto size = p["default"].size();
            defaultVal = p["default"][std::min(index, (int)size - 1)].as<float>();
        }

        ParameterConfig config {range, defaultVal};
        config.name = std::move(configName);
        config.reverse = reverse;
        config.textFunction = [&] (const Parameter& p, float val) -> DisplayValue { return {juce::String(val)}; };
        config.valueFunction = [&] (const Parameter& p, const juce::String& s) -> float { return s.getFloatValue(); };

        auto syncType = getString(p, "sync", "normal");

        if (type == "number") {
            // nothing to do
        } else if (type == "percent") {
            config.textFunction = DisplayFunctions::percentDisplay;
            config.valueFunction = DisplayFunctions::percentInput;
        } else if (type == "db") {
            config.textFunction = DisplayFunctions::dbDisplay;
            config.valueFunction = DisplayFunctions::dbInput;
        } else if (type == "time") {
            config.textFunction = DisplayFunctions::timeDisplay;
            config.valueFunction = DisplayFunctions::timeInput;
        } else if (type == "samples") {
            config.textFunction = [&] (const Parameter& p, float samples) -> DisplayValue {
                return DisplayFunctions::timeDisplay(p, samples/processor.getLastSampleRate());
            };
            config.valueFunction = [&] (const Parameter& p, juce::String s) -> float {
                return DisplayFunctions::timeInput(p, s) * processor.getLastSampleRate();
            };
        } else if (type == "freq") {
            config.textFunction = DisplayFunctions::freqDisplay;
            config.valueFunction = DisplayFunctions::freqInput;
        } else if (type == "degrees") {
            config.textFunction = DisplayFunctions::degreeDisplay;
            config.valueFunction = DisplayFunctions::degreeInput;
        } else if (type == "toggle") {
            config.range = {0, 1, 1};
        } else if (type == "semitone") {
            config.textFunction = DisplayFunctions::semitoneDisplay;
            config.valueFunction = DisplayFunctions::semitoneInput;
        } else if (type == "cent") {
            config.textFunction = DisplayFunctions::centDisplay;
            config.valueFunction = DisplayFunctions::centInput;
        } else if (type == "sync") {
            config.textFunction = DisplayFunctions::syncDisplay;
            config.valueFunction = DisplayFunctions::syncInput;
            config.conversionFunction = [&, syncType](float proportion) {
                auto v = (processor.getSyncTimeSeconds(proportion));
                return (syncType == "inverse") ? 1.f / v : v;
            };
        } else if (type == "sync-dotted") {
            config.textFunction = [](const Parameter& p, float t) -> DisplayValue {
                return {DisplayFunctions::syncDisplay(p, t).value, "d"};
            };
            config.valueFunction = [](const Parameter& p, juce::String frac) {
                return DisplayFunctions::syncInput(p,
                           frac.replace("d", "", true));
            };

            config.conversionFunction = [&, syncType](float proportion) {
                auto v = (processor.getSyncTimeSeconds(proportion) * (3.f/2.f));
                return (syncType == "inverse") ? 1.f / v : v;
            };
        } else if (type == "sync-triplet") {
            config.textFunction = [](const Parameter& p, float t) -> DisplayValue {
                return {DisplayFunctions::syncDisplay(p, t).value, "t"};
            };
            config.valueFunction = [](const Parameter& p, juce::String frac) {
                return DisplayFunctions::syncInput(p,
                               frac.replace("t", "", true));
            };
            config.conversionFunction = [&, syncType](float proportion) {
                auto v = (processor.getSyncTimeSeconds(proportion) * (2.f/3.f));
                return (syncType == "inverse") ? 1.f / v : v;
            };
        } else if (type == "choice") {
            auto choices = p["choices"].as<std::vector<std::string>>();
            config.textFunction = [choices](const Parameter& p, float choice)->DisplayValue {
                return {choices[choice]};
            };
            config.valueFunction = [choices](const Parameter& p, juce::String choice) {
                return std::find(choices.begin(), choices.end(), choice) - choices.begin();
            };
            config.range = {0, (float)choices.size()-1, 1};
        } else if (type == "ratio") {
            auto ratioParams = p["ratio"];
            juce::String ratioParamName = ratioParams["name"].as<std::string>();
            Parameter* ratioParam;

            if (ratioParams["type"].as<std::string>() == "stream")
                ratioParam = processor.getParameter(ratioParamName + "-" + juce::String(index));
            else
                ratioParam = processor.getParameter(ratioParamName);

            juce::String ratioParamDisplayName = getString(ratioParams, "display",
                                                           ratioParam->getName(100));

            config.textFunction = [&, ratioParamDisplayName](const Parameter& p, float v) -> DisplayValue {
                if (v > 1) return {juce::String(v, 2)+":1", "-> " + ratioParamDisplayName};
                else return {"1:"+juce::String(1/v, 2), "-> " + ratioParamDisplayName};
            };

            config.valueFunction = [&, ratioParamDisplayName](const Parameter& p, juce::String v) -> float {
                v = v.upToFirstOccurrenceOf("->", false, true);
                juce::StringArray ratio;
                ratio.addTokens(v, ":", "");
                if (ratio.size() != 2) return v.getFloatValue();

                return ratio[0].getFloatValue() / ratio[1].getFloatValue();
            };

            config.conversionFunction = [&, ratioParam](float v) {
                return ratioParam->getModVal() * v;
            };
        }

        return config;
    }

    std::unique_ptr<Parameter> ParameterLoader::loadParameter(const juce::String& uid, YAML::Node p,
                                                              const juce::String& namePrefix, int index) {
        auto name = namePrefix + str(p["name"]);
        auto internal = getBool(p, "internal", false);

        // Multi-mode
        if (p["modes"]) {
            std::vector<ParameterConfig> configs;
            for (auto mode : p["modes"]) {
                juce::String configName = mode.first.as<std::string>();
                configs.push_back(loadConfig(uid, configName, mode.second, index));
            }

            return std::make_unique<Parameter>(uid, name, configs, internal);
        }

        // Single-mode
        auto config = loadConfig(uid,"default", p, index);

        return std::make_unique<Parameter>(uid, name, config, internal);
    }
}