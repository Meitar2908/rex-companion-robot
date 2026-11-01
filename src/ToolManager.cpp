#include "ToolManager.h"

void ToolManager::appendToolSchemas(JsonArray toolsArray) const {
  JsonObject weather = toolsArray.add<JsonObject>();
  weather["name"] = "get_weather";
  weather["description"] =
    "Current conditions or forecast for a location. Use for any weather "
    "question: temperature, rain, forecast, whether to bring a jacket, etc.";
  JsonObject weatherSchema = weather["input_schema"].to<JsonObject>();
  weatherSchema["type"] = "object";
  JsonObject weatherProps = weatherSchema["properties"].to<JsonObject>();
  JsonObject wLocation = weatherProps["location"].to<JsonObject>();
  wLocation["type"] = "string";
  wLocation["description"] =
    "Only set this if the user NAMES a different city. If they ask about "
    "weather \"here\" / \"where I am\" / generically with no city named, "
    "OMIT this field entirely — the default location IS their actual "
    "location, not an unknown. Never ask the user where they are.";
  JsonObject wDate = weatherProps["date"].to<JsonObject>();
  wDate["type"] = "string";
  wDate["description"] = "\"today\", \"tomorrow\", a weekday name, or omit for today.";

  JsonObject timeTool = toolsArray.add<JsonObject>();
  timeTool["name"] = "get_time";
  timeTool["description"] =
    "Current date/time, optionally in another city's timezone, or how many "
    "days from now a given date is. Use for any \"what time/day is it\" "
    "question, or day-counting questions like \"how many days until X\".";
  JsonObject timeSchema = timeTool["input_schema"].to<JsonObject>();
  timeSchema["type"] = "object";
  JsonObject timeProps = timeSchema["properties"].to<JsonObject>();
  JsonObject tLocation = timeProps["location"].to<JsonObject>();
  tLocation["type"] = "string";
  tLocation["description"] =
    "Only set this if the user NAMES a different city's time. If they ask "
    "about the time \"here\" / \"where I am\" / generically, OMIT this "
    "field entirely — the default location IS their actual location.";
  JsonObject tTargetDate = timeProps["target_date"].to<JsonObject>();
  tTargetDate["type"] = "string";
  tTargetDate["description"] =
    "ISO date (YYYY-MM-DD) to count days until, e.g. for \"how many days "
    "until Christmas\" resolve the actual date yourself and pass it here. "
    "Omit unless the user is asking about a countdown to a specific date.";

  JsonObject calc = toolsArray.add<JsonObject>();
  calc["name"] = "calculate";
  calc["description"] =
    "Exact arithmetic. Use for any calculation, percentage, or unit "
    "conversion — translate the question into a plain expression yourself "
    "first (\"18% of 450\" -> \"450*0.18\", \"70F to C\" -> \"(70-32)*5/9\") "
    "and this computes it exactly rather than you doing the math yourself.";
  JsonObject calcSchema = calc["input_schema"].to<JsonObject>();
  calcSchema["type"] = "object";
  JsonObject calcProps = calcSchema["properties"].to<JsonObject>();
  JsonObject calcExpr = calcProps["expression"].to<JsonObject>();
  calcExpr["type"] = "string";
  calcExpr["description"] = "A plain arithmetic expression using + - * / ( ) and numbers only.";
  JsonArray calcRequired = calcSchema["required"].to<JsonArray>();
  calcRequired.add("expression");

  JsonObject places = toolsArray.add<JsonObject>();
  places["name"] = "find_places";
  places["description"] =
    "Nearby points of interest (cafes, restaurants, pharmacies, bars, "
    "etc.) by category. Use for \"find a X nearby\" / \"where's the "
    "closest X\" questions.";
  JsonObject placesSchema = places["input_schema"].to<JsonObject>();
  placesSchema["type"] = "object";
  JsonObject placesProps = placesSchema["properties"].to<JsonObject>();
  JsonObject placesCategory = placesProps["category"].to<JsonObject>();
  placesCategory["type"] = "string";
  placesCategory["description"] =
    "A short category keyword: cafe, restaurant, fast_food, pharmacy, bar, "
    "bank, hospital, supermarket, etc.";
  JsonObject placesLocation = placesProps["location"].to<JsonObject>();
  placesLocation["type"] = "string";
  placesLocation["description"] =
    "Only set this if the user NAMES a different city. If they ask about "
    "something \"near me\" / \"nearby\" with no city named, OMIT this "
    "field entirely — the default location IS their actual location.";
  JsonArray placesRequired = placesSchema["required"].to<JsonArray>();
  placesRequired.add("category");

  JsonObject sports = toolsArray.add<JsonObject>();
  sports["name"] = "get_sports_results";
  sports["description"] =
    "Most recent result and next scheduled fixture for a sports team. Use "
    "for \"who won the X game\" / \"when does X play next\" questions. "
    "NOT live in-progress scores — completed results and schedule only.";
  JsonObject sportsSchema = sports["input_schema"].to<JsonObject>();
  sportsSchema["type"] = "object";
  JsonObject sportsProps = sportsSchema["properties"].to<JsonObject>();
  JsonObject sportsTeam = sportsProps["team"].to<JsonObject>();
  sportsTeam["type"] = "string";
  sportsTeam["description"] = "Team name as the user said it.";
  JsonArray sportsRequired = sportsSchema["required"].to<JsonArray>();
  sportsRequired.add("team");

  JsonObject search = toolsArray.add<JsonObject>();
  search["name"] = "web_search";
  search["description"] =
    "General web search — real indexed web results, not just common "
    "knowledge. Use for specific facts/numbers you're not certain of "
    "(box office figures, stats, prices, \"who is the current X\"), or "
    "anything else not covered by the other tools. For dated news "
    "headlines specifically, prefer get_news instead.";
  JsonObject searchSchema = search["input_schema"].to<JsonObject>();
  searchSchema["type"] = "object";
  JsonObject searchProps = searchSchema["properties"].to<JsonObject>();
  JsonObject searchQuery = searchProps["query"].to<JsonObject>();
  searchQuery["type"] = "string";
  searchQuery["description"] = "The search query.";
  JsonArray searchRequired = searchSchema["required"].to<JsonArray>();
  searchRequired.add("query");

  JsonObject news = toolsArray.add<JsonObject>();
  news["name"] = "get_news";
  news["description"] =
    "Recent news headlines on a topic. Use for \"latest news about X\" / "
    "\"what's happening with X\" questions — quick_search will NOT have "
    "these, this is the right tool for current-events questions. Results "
    "can run up to ~12 hours behind real time on the free tier.";
  JsonObject newsSchema = news["input_schema"].to<JsonObject>();
  newsSchema["type"] = "object";
  JsonObject newsProps = newsSchema["properties"].to<JsonObject>();
  JsonObject newsTopic = newsProps["topic"].to<JsonObject>();
  newsTopic["type"] = "string";
  newsTopic["description"] = "The subject to find news about.";
  JsonArray newsRequired = newsSchema["required"].to<JsonArray>();
  newsRequired.add("topic");

  JsonObject timer = toolsArray.add<JsonObject>();
  timer["name"] = "set_timer";
  timer["description"] =
    "Sets a TIME-DELAYED alarm — REX will speak up on its own once the "
    "given duration has elapsed, without being asked again. Covers both "
    "plain timers (\"set a timer for 5 minutes\") and duration-based "
    "reminders (\"remind me in 20 minutes to check the oven\" — put "
    "\"check the oven\" in label). Only for things tied to a countdown "
    "in seconds — NOT for remembering a fact about the user for a "
    "FUTURE conversation (that's the separate [LEARN:] tag, not a tool "
    "call at all) and not for anything without a concrete duration.";
  JsonObject timerSchema = timer["input_schema"].to<JsonObject>();
  timerSchema["type"] = "object";
  JsonObject timerProps = timerSchema["properties"].to<JsonObject>();
  JsonObject timerSeconds = timerProps["seconds"].to<JsonObject>();
  timerSeconds["type"] = "integer";
  timerSeconds["description"] = "How many seconds from now the timer should fire.";
  JsonObject timerLabel = timerProps["label"].to<JsonObject>();
  timerLabel["type"] = "string";
  timerLabel["description"] = "Optional short label, e.g. \"pasta\" or \"check the oven\".";
  JsonArray timerRequired = timerSchema["required"].to<JsonArray>();
  timerRequired.add("seconds");
}

ToolResult ToolManager::execute(const String& toolName, JsonObjectConst params) {
  if (toolName == "get_weather") {
    String location = params["location"] | "";
    String date = params["date"] | "";
    return weather_.fetch(location, date);
  }
  if (toolName == "get_time") {
    String location = params["location"] | "";
    String targetDate = params["target_date"] | "";
    return time_.fetch(location, targetDate);
  }
  if (toolName == "calculate") {
    String expression = params["expression"] | "";
    return calculator_.evaluate(expression);
  }
  if (toolName == "find_places") {
    String category = params["category"] | "";
    String location = params["location"] | "";
    return places_.search(category, location);
  }
  if (toolName == "get_sports_results") {
    String team = params["team"] | "";
    return sports_.lookup(team);
  }
  if (toolName == "web_search") {
    String query = params["query"] | "";
    return webSearch_.search(query);
  }
  if (toolName == "get_news") {
    String topic = params["topic"] | "";
    return news_.search(topic);
  }
  if (toolName == "set_timer") {
    ToolResult r;
    if (!timers_) {
      r.error = "timers aren't available right now";
      return r;
    }
    uint32_t seconds = params["seconds"] | 0;
    String label = params["label"] | "";
    return timers_->set(label, seconds);
  }
  ToolResult r;
  r.error = "unknown tool: " + toolName;
  return r;
}
