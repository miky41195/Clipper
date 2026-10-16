#pragma once
#include "ui/Fonts.h"
#include <imgui.h>
#include <string>
#include <vector>

// Small set of custom widgets that give Clipper its look. All sizes are in unscaled pixels.
namespace clipper::ui {

float controlWidth();  // width of the right-hand control column in a settings row

void pageHeader(const UiFonts& f, const char* title, const char* desc);
void beginCard(const char* id);
void endCard();
void cardTitle(const UiFonts& f, const char* title, const char* hint = nullptr);

// Settings row: label (+hint) on the left, control column on the right. Call endRow() after the control.
void beginRow(const UiFonts& f, const char* label, const char* hint = nullptr);
void endRow(bool separator = true);
void alignRight(float itemWidth);  // inside a row: move the cursor so an item of this width ends at the column edge

bool toggle(const char* id, bool* value);
bool segmented(const char* id, const std::vector<std::string>& items, int* index, float width = 0);
bool combo(const char* id, const std::vector<std::string>& items, int* index, float width = 0);
bool stepSlider(const char* id, const std::vector<int>& steps, int* value, const std::string& label, float width = 0);
bool primaryButton(const char* label, bool enabled = true, float width = 0);
bool secondaryButton(const char* label, bool enabled = true, float width = 0);
bool iconButton(const UiFonts& f, const char* id, const char* glyph, const char* tooltip = nullptr);
bool navItem(const UiFonts& f, const char* glyph, const char* label, bool selected);
void keycaps(const UiFonts& f, const std::string& combo);
float drawKeycaps(const UiFonts& f, const std::string& combo, ImVec2 pos, float height);  // no layout; returns width
bool colorSwatch(const char* id, ImU32 color, bool selected);
void statusPill(const UiFonts& f, ImU32 dot, const std::string& text);
void mutedText(const UiFonts& f, const std::string& text, bool wrap = true);

}  // namespace clipper::ui
