#include "ReactionEngine.h"
#include "ExpressionTable.h"

namespace {

struct Keyword { Expression expr; const char* phrase; };

// Original keyword set, hand-written for this project. React to what
// was SAID, not merely to sentiment words — e.g. "I lost my wallet"
// scores Worried, not Sad; "boo!" scores Scared, not Angry.
const Keyword kKeywords[] = {
  {Expression::Excited,     "got the job"},
  {Expression::Excited,     "i won"},
  {Expression::Excited,     "we won"},
  {Expression::Excited,     "can't wait"},
  {Expression::Excited,     "so excited"},
  {Expression::Excited,     "amazing news"},

  {Expression::Happy,       "awesome"},
  {Expression::Happy,       "that's great"},
  {Expression::Happy,       "sounds great"},
  {Expression::Happy,       "i'm happy"},
  {Expression::Happy,       "glad to"},
  {Expression::Happy,       "nice job"},

  {Expression::Amused,      "hilarious"},
  {Expression::Amused,      "so funny"},
  {Expression::Amused,      "lol"},
  {Expression::Amused,      "haha"},

  {Expression::Curious,     "what is"},
  {Expression::Curious,     "what's that"},
  {Expression::Curious,     "how does"},
  {Expression::Curious,     "i wonder"},
  {Expression::Curious,     "curious"},

  {Expression::Thinking,    "let me think"},
  {Expression::Thinking,    "hold on"},
  {Expression::Thinking,    "give me a second"},
  {Expression::Thinking,    "hmm"},

  {Expression::Confused,    "don't understand"},
  {Expression::Confused,    "i'm confused"},
  {Expression::Confused,    "makes no sense"},
  {Expression::Confused,    "i'm lost"},

  {Expression::Skeptical,   "are you sure"},
  {Expression::Skeptical,   "really?"},
  {Expression::Skeptical,   "doubt it"},
  {Expression::Skeptical,   "not convinced"},

  {Expression::Suspicious,  "don't believe you"},
  {Expression::Suspicious,  "something's off"},
  {Expression::Suspicious,  "don't trust"},
  {Expression::Suspicious,  "sounds fishy"},

  {Expression::Surprised,   "what?!"},
  {Expression::Surprised,   "no way"},
  {Expression::Surprised,   "seriously?"},
  {Expression::Surprised,   "whoa"},

  {Expression::Amazed,      "incredible"},
  {Expression::Amazed,      "unbelievable"},
  {Expression::Amazed,      "did you know"},
  {Expression::Amazed,      "that's amazing"},

  {Expression::Worried,     "lost my wallet"},
  {Expression::Worried,     "i'm worried"},
  {Expression::Worried,     "what if"},
  {Expression::Worried,     "nervous about"},

  {Expression::Sad,         "i'm sad"},
  {Expression::Sad,         "really sad"},
  {Expression::Sad,         "heartbroken"},
  {Expression::Sad,         "feeling down"},

  {Expression::Annoyed,     "annoying"},
  {Expression::Annoyed,     "so annoying"},
  {Expression::Annoyed,     "ugh"},

  {Expression::Frustrated,  "won't work"},
  {Expression::Frustrated,  "doesn't work"},
  {Expression::Frustrated,  "i'm frustrated"},
  {Expression::Frustrated,  "isn't working"},

  {Expression::Angry,       "i'm furious"},
  {Expression::Angry,       "i'm so angry"},
  {Expression::Angry,       "pissed off"},
  {Expression::Angry,       "makes me mad"},

  {Expression::Scared,      "boo!"},
  {Expression::Scared,      "i'm scared"},
  {Expression::Scared,      "terrified"},
  {Expression::Scared,      "that scared me"},

  {Expression::Sleepy,      "i'm exhausted"},
  {Expression::Sleepy,      "so tired"},
  {Expression::Sleepy,      "need sleep"},
  {Expression::Sleepy,      "sleepy"},

  {Expression::Unimpressed, "whatever"},
  {Expression::Unimpressed, "meh"},
  {Expression::Unimpressed, "not impressed"},
  {Expression::Unimpressed, "don't care"},
};
constexpr int kKeywordCount = sizeof(kKeywords) / sizeof(Keyword);

String toLowerCopy(const String& in) {
  String out = in;
  out.toLowerCase();
  return out;
}

} // namespace

Reaction ReactionEngine::classify(const String& text) const {
  String lower = toLowerCopy(text);

  int score[(int)Expression::COUNT] = {0};
  for (int i = 0; i < kKeywordCount; i++) {
    if (lower.indexOf(kKeywords[i].phrase) >= 0)
      score[(int)kKeywords[i].expr]++;
  }

  Expression best = Expression::Neutral;
  int bestScore = 0;
  for (int i = 0; i < (int)Expression::COUNT; i++) {
    if (score[i] > bestScore) {
      bestScore = score[i];
      best = (Expression)i;
    }
  }

  int exclaims = 0, questions = 0, capsLetters = 0, letters = 0;
  for (size_t i = 0; i < text.length(); i++) {
    char c = text[i];
    if (c == '!') exclaims++;
    if (c == '?') questions++;
    if (isAlpha(c)) { letters++; if (isUpperCase(c)) capsLetters++; }
  }
  float capsRatio = letters > 0 ? (float)capsLetters / (float)letters : 0.0f;

  Reaction r;
  r.expression = best;

  if (bestScore == 0) {
    r.intensity = 0.0f;
  } else {
    float intensity = 0.45f + (bestScore - 1) * 0.20f
                     + exclaims * 0.08f + questions * 0.04f + capsRatio * 0.20f;
    if (intensity > 1.0f) intensity = 1.0f;
    if (intensity < 0.15f) intensity = 0.15f;
    r.intensity = intensity;
  }

  const ExpressionDef& def = expressionDef(best);
  r.gazeX     = def.gazeX;
  r.gazeY     = def.gazeY;
  r.blink     = def.blink;
  r.attackMs  = def.attackMs;
  r.holdMs    = def.holdMs;
  r.releaseMs = def.releaseMs;
  return r;
}
