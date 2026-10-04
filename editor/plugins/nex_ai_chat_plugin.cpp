/**************************************************************************/
/*  nex_ai_chat_plugin.cpp                                                */
/*  NEX AI — chat dock lateral da Nexus Engine                             */
/*  Spec: NEX_AI.md (documento oficial do módulo NEX)                      */
/**************************************************************************/

#include "editor/plugins/nex_ai_chat_plugin.h"

#include "core/math/color.h"
#include "core/object/callable_mp.h"
#include "core/variant/variant.h"
#include "core/string/ustring.h"
#include "editor/plugins/editor_plugin.h"
#include "scene/gui/button.h"
#include "scene/gui/box_container.h"
#include "scene/gui/label.h"
#include "scene/gui/line_edit.h"
#include "scene/gui/rich_text_label.h"

static const Color NEX_PURPLE(0.659f, 0.333f, 0.969f, 1.0f);
static const Color NEX_PURPLE_LIGHT(0.769f, 0.518f, 0.988f, 1.0f);

NexAIChatPlugin::NexAIChatPlugin() {
	panel = memnew(VBoxContainer);
	panel->set_name(TTR("NEX"));
	panel->set_custom_minimum_size(Size2(340, 0));

	// Titulo.
	Label *title = memnew(Label);
	title->set_text(TTR("NEX — Nexus Engine"));
	title->add_theme_color_override("font_color", NEX_PURPLE_LIGHT);
	title->add_theme_font_size_override("font_size", 20);
	panel->add_child(title);

	// Historico do chat.
	chat = memnew(RichTextLabel);
	chat->set_use_bbcode(true);
	chat->set_fit_content(true);
	chat->set_scroll_follow(true);
	chat->set_custom_minimum_size(Size2(0, 300));
	chat->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	panel->add_child(chat);

	// Botoes de permissao (aparecem dentro do chat quando a NEX pede).
	perm_row = memnew(HBoxContainer);
	btn_yes = memnew(Button);
	btn_yes->set_text(TTR("SIM, PODE FAZER"));
	btn_yes->add_theme_color_override("font_color", NEX_PURPLE_LIGHT);
	perm_row->add_child(btn_yes);
	btn_no = memnew(Button);
	btn_no->set_text(TTR("NÃO, SÓ ME EXPLICA"));
	perm_row->add_child(btn_no);
	perm_row->hide();
	panel->add_child(perm_row);

	btn_yes->connect("pressed", callable_mp(this, &NexAIChatPlugin::_on_permission).bind(true));
	btn_no->connect("pressed", callable_mp(this, &NexAIChatPlugin::_on_permission).bind(false));

	// Campo de entrada + botao enviar.
	HBoxContainer *entry_row = memnew(HBoxContainer);
	input = memnew(LineEdit);
	input->set_placeholder(TTR("Fale com a NEX... ex: faz um mapa pra pvp"));
	input->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	input->connect("text_submitted", callable_mp(this, &NexAIChatPlugin::_process_message));
	entry_row->add_child(input);

	Button *send = memnew(Button);
	send->set_text(TTR("Enviar"));
	send->connect("pressed", callable_mp(this, &NexAIChatPlugin::_process_message).bind(String()));
	entry_row->add_child(send);
	panel->add_child(entry_row);

	// Saldo de creditos (conta do dono = ilimitado).
	credits = memnew(Label);
	credits->set_text(TTR("Créditos: ∞"));
	credits->add_theme_color_override("font_color", NEX_PURPLE);
	credits->add_theme_font_size_override("font_size", 14);
	panel->add_child(credits);

	add_control_to_dock(DOCK_SLOT_RIGHT_UL, panel);

	// Boas-vindas.
	_append_chat("NEX", TTR("Oi! Eu sou a NEX, a IA da Nexus Engine e sua parceira de criação. 🟣\n\nPode me pedir qualquer coisa em português: colisão, arma, animação, mapa PvP, movimento, até o servidor do seu jogo.\n\nEu peço permissão antes de mexer em tudo e trabalho ao vivo, mostrando cada passo."), NEX_PURPLE_LIGHT);
}

void NexAIChatPlugin::_append_chat(const String &p_who, const String &p_text, const Color &p_color) {
	String color_hex = p_color.to_html(false);
	chat->append_text("[color=#" + color_hex + "][b]" + p_who + ":[/b][/color] " + p_text + "\n\n");
}

void NexAIChatPlugin::_process_message(const String &p_text) {
	String txt = p_text.strip_edges();
	if (txt.is_empty()) {
		// Veio do botao Enviar: pega o texto do campo.
		txt = input->get_text().strip_edges();
	}
	if (txt.is_empty()) {
		return;
	}
	input->set_text("");

	_append_chat(TTR("Você"), txt, Color(1, 1, 1, 1));

	// TRAVA ANTI-CÓPIA (secao 6 do NEX_AI.md).
	static const char *forbidden[] = {
		"free fire", "freefire", "pubg", "valorant", "cs2", "counter strike",
		"call of duty", "gta", "fortnite", "minecraft", "roblox", "among us",
		"fall guys", "brawl stars", "clash royale", "league of legends",
		"stardew valley", "terraria", "subway surfers", "candy crush",
		"copia", "copiar", "clona", "clonar", "faz igual", "igualzinho", "mesma coisa"
	};
	String low = txt.to_lower();
	String padded = " " + low + " ";
	for (const char *word : forbidden) {
		if (low.contains(String(word)) || padded.contains(" " + String(word) + " ")) {
			_append_chat("NEX", TTR("Não posso copiar jogos de outras empresas pra te proteger de processo, mas posso te ajudar a criar um jogo AINDA MELHOR com sua cara. Bora criar um original do mesmo estilo? 🎮"), NEX_PURPLE_LIGHT);
			return;
		}
	}

	// Fluxo normal: pede permissao antes de agir (secao 5 do NEX_AI.md).
	_append_chat("NEX", vformat(TTR("Posso fazer! Quero mexer no projeto pra criar: \"%s\". Você permite?"), txt), NEX_PURPLE_LIGHT);
	perm_row->show();
}

void NexAIChatPlugin::_on_permission(bool p_allow) {
	perm_row->hide();
	if (p_allow) {
		// MODO AO VIVO (secao 5.1 do NEX_AI.md).
		_append_chat("NEX", TTR("Beleza! Entrando no modo ao vivo... 🟣\n\n(A conexão com o cérebro da NEX ainda está em desenvolvimento nesta versão. Nas próximas versões eu edito sua cena na sua frente, passo a passo, e te aviso: \"Prontinho!\")"), NEX_PURPLE_LIGHT);
	} else {
		_append_chat("NEX", TTR("Sem problema! Vou te explicar na tela como fazer na mão, passo a passo. Só me dizer o que você quer aprender."), NEX_PURPLE_LIGHT);
	}
}
