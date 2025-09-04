#pragma once
#include <unordered_map>
#include <string>
#include "core/types.hpp"

struct Weights { std::unordered_map<std::string, double> w; };
struct Scores  { std::unordered_map<std::string, double> s; };

struct Decision {
    double combined_score{50.0};
    Signal action{Signal::Neutral};
};

inline Decision decide(const Scores& sc, const Weights& w, double up=70.0, double down=30.0){
    double sumw=0.0, acc=0.0;
    for (auto& [id, s] : sc.s){
        auto it = w.w.find(id); if (it==w.w.end()) continue;
        acc  += it->second * (s/100.0);
        sumw += it->second;
    }
    double cs = (sumw>0? acc/sumw : 0.5) * 100.0;
    Signal sig = (cs>up? Signal::Long : (cs<down? Signal::Short : Signal::Neutral));
    return {cs, sig};
}
// decision.hpp from canvas
