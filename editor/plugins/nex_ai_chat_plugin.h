/**************************************************************************/
/*  nex_ai_chat_plugin.h                                                  */
/*  NEX AI — Nexus Engine                                                  */
/**************************************************************************/

#ifndef NEX_AI_CHAT_PLUGIN_H
#define NEX_AI_CHAT_PLUGIN_H

#include "editor/plugins/editor_plugin.h"

class VBoxContainer;
class HBoxContainer;
class RichTextLabel;
class LineEdit;
class Button;
class Label;

class NexAIChatPlugin : public EditorPlugin {
	GDCLASS(NexAIChatPlugin, EditorPlugin);

	VBoxContainer *panel = nullptr;
	RichTextLabel *chat = nullptr;
	LineEdit *input = nullptr;
	HBoxContainer *perm_row = nullptr;
	Button *btn_yes = nullptr;
	Button *btn_no = nullptr;
	Label *credits = nullptr;

	void _append_chat(const String &p_who, const String &p_text, const Color &p_color);
	void _process_message(const String &p_text);
	void _on_permission(bool p_allow);

protected:
	static void _bind_methods() {}

public:
	NexAIChatPlugin();
};

#endif // NEX_AI_CHAT_PLUGIN_H
