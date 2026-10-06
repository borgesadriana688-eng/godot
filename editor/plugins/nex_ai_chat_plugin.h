/**************************************************************************/
/*  nex_ai_chat_plugin.h                                                  */
/*  NEX AI — chat dock lateral da Nexus Engine                             */
/*  Spec: NEX_AI.md (documento oficial do módulo NEX)                      */
/**************************************************************************/

#ifndef NEX_AI_CHAT_PLUGIN_H
#define NEX_AI_CHAT_PLUGIN_H

#include "editor/plugins/editor_plugin.h"

#include "core/math/color.h"
#include "core/templates/vector.h"
#include "core/variant/dictionary.h"
#include "core/variant/variant.h"
#include "scene/gui/box_container.h"
#include "scene/gui/button.h"
#include "scene/gui/label.h"
#include "scene/gui/line_edit.h"
#include "scene/gui/rich_text_label.h"

class NexAIChatPlugin : public EditorPlugin {
	GDCLASS(NexAIChatPlugin, EditorPlugin)

public:
	// O que a NEX vai executar no modo ao vivo.
	enum PendingAction {
		ACTION_NONE,
		ACTION_MAP,
		ACTION_COLLISION,
		ACTION_WEAPON,
		ACTION_HUD,
		ACTION_LOBBY,
		ACTION_TREE,
		ACTION_CHAR,
		ACTION_HOUSE,
		ACTION_CAR,
		ACTION_RAMP,
		ACTION_CRYSTAL,
		ACTION_COIN,
		ACTION_PLATFORM,
		ACTION_GENERIC,
	};

private:
	VBoxContainer *panel = nullptr;
	HBoxContainer *quick_row = nullptr;
	HBoxContainer *quick_row2 = nullptr;
	RichTextLabel *chat = nullptr;
	HBoxContainer *perm_row = nullptr;
	Button *btn_yes = nullptr;
	Button *btn_no = nullptr;
	LineEdit *input = nullptr;
	Label *credits = nullptr;

	PendingAction pending_action = ACTION_NONE;
	String pending_text;

	// Dados extraidos do pedido do dev (cor, quantidade, tamanho).
	int pending_count = 1;
	float pending_scale = 1.0f;
	Color pending_color = Color(0.66f, 0.33f, 0.97f);
	bool pending_color_valid = false;

	// MODELOS REAIS: asset .glb baixado da internet (release nex-assets-v1).
	String pending_asset_file;
	String pending_asset_url;
	String pending_brain_object;
	String _asset_save_path;
	bool pending_have_real = false;
	class HTTPRequest *_brain_req = nullptr;
	class HTTPRequest *_asset_req = nullptr;

	// Fila de passos do modo ao vivo (executados um por vez com pausa,
	// narrando no chat, pra dar o efeito "fazendo na tela").
	Vector<String> live_narrations;
	Vector<int> live_kinds; // ver switch em _live_next_step()
	int live_index = 0;
	int live_total = 0;

	Button *_make_quick_button(const String &p_label, const String &p_message);
	void _append_chat(const String &p_who, const String &p_text, const Color &p_color);
	void _show_tutorial();
	void _process_message(const String &p_text);
	void _answer_question(const String &p_low);
	void _classify_and_ask(const String &p_low);
	void _brain_ask(const String &p_text);
	void _on_brain_reply(int p_result, int p_response_code, const PackedStringArray &p_headers, const PackedByteArray &p_body);
	void _on_asset_done(int p_result, int p_response_code, const PackedStringArray &p_headers, const PackedByteArray &p_body);
	void _apply_brain_command(const Dictionary &p_cmd);
	bool _spawn_real(Node *p_root, const String &p_base_name, float p_spacing);
	void _on_permission(bool p_allow);
	void _start_live();
	void _live_next_step();
	void _queue_live(int p_kind, const String &p_narration);
	bool _add_node_live(Node *p_parent, Node *p_child, const String &p_action_name);

protected:
	static void _bind_methods() {}

public:
	void _notification(int p_what);
	NexAIChatPlugin();
};

#endif // NEX_AI_CHAT_PLUGIN_H
