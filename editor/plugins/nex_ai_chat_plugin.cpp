/**************************************************************************/
/*  nex_ai_chat_plugin.cpp                                                */
/*  NEX AI — chat dock lateral da Nexus Engine                             */
/*  Spec: NEX_AI.md (documento oficial do módulo NEX)                      */
/*  v0.2 — MODO AO VIVO: cria nós de verdade na cena editada,              */
/*  narrando cada passo no chat.                                           */
/**************************************************************************/

#include "editor/plugins/nex_ai_chat_plugin.h"

#include "core/math/color.h"
#include "core/object/callable_mp.h"
#include "core/variant/variant.h"
#include "core/string/ustring.h"
#include "editor/editor_data.h"
#include "editor/editor_node.h"
#include "editor/editor_undo_redo_manager.h"
#include "editor/plugins/editor_plugin.h"
#include "scene/3d/physics/collision_shape_3d.h"
#include "scene/3d/light_3d.h"
#include "scene/3d/mesh_instance_3d.h"
#include "scene/3d/node_3d.h"
#include "scene/3d/physics/static_body_3d.h"
#include "scene/gui/box_container.h"
#include "scene/gui/button.h"
#include "scene/gui/color_rect.h"
#include "scene/gui/control.h"
#include "scene/gui/label.h"
#include "scene/gui/line_edit.h"
#include "scene/gui/rich_text_label.h"
#include "scene/main/canvas_layer.h"
#include "scene/main/node.h"
#include "scene/resources/3d/box_shape_3d.h"
#include "scene/resources/3d/primitive_meshes.h"
#include "scene/resources/material.h"

// OBS: nao usar emoji (caracteres acima de U+FFFF) em nenhum texto desta
// tela. A fonte do editor Android nao tem esses glifos e eles aparecem
// quebrados. Letras acentuadas normais (ã, ç, é) funcionam bem.

static const Color NEX_PURPLE(0.659f, 0.333f, 0.969f, 1.0f);
static const Color NEX_PURPLE_LIGHT(0.769f, 0.518f, 0.988f, 1.0f);

// Passos do modo ao vivo.
enum NexLiveStep {
	STEP_GROUND,
	STEP_WALLS,
	STEP_LIGHT,
	STEP_COLLISION,
	STEP_GUN,
	STEP_HUD,
	STEP_LOBBY,
	STEP_GENERIC,
};

static Ref<StandardMaterial3D> _nex_mat(float p_r, float p_g, float p_b, float p_a = 1.0f) {
	Ref<StandardMaterial3D> mat;
	mat.instantiate();
	mat->set_albedo(Color(p_r, p_g, p_b, p_a));
	mat->set_roughness(0.45f);
	return mat;
}

Button *NexAIChatPlugin::_make_quick_button(const String &p_label, const String &p_message) {
	Button *b = memnew(Button);
	b->set_text(p_label);
	b->set_custom_minimum_size(Size2(0, 40)); // alvo de toque confortavel no celular
	b->add_theme_font_size_override("font_size", 14);
	b->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	if (p_message == "__HELP__") {
		b->connect("pressed", callable_mp(this, &NexAIChatPlugin::_show_tutorial));
	} else {
		b->connect("pressed", callable_mp(this, &NexAIChatPlugin::_process_message).bind(p_message));
	}
	return b;
}

NexAIChatPlugin::NexAIChatPlugin() {
	panel = memnew(VBoxContainer);
	panel->set_name(TTR("NEX"));
	panel->set_custom_minimum_size(Size2(340, 0));
	panel->add_theme_constant_override("separation", 6);

	Label *title = memnew(Label);
	title->set_text(TTR("NEX - sua ajudante"));
	title->add_theme_color_override("font_color", NEX_PURPLE_LIGHT);
	title->add_theme_font_size_override("font_size", 20);
	panel->add_child(title);

	Label *subtitle = memnew(Label);
	subtitle->set_text(TTR("Toque num botão ou escreva embaixo. Depois de autorizar, eu crio as coisas na sua tela ao vivo."));
	subtitle->add_theme_font_size_override("font_size", 13);
	subtitle->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
	panel->add_child(subtitle);

	// Botoes rapidos (nao precisa digitar nada pra comecar).
	quick_row = memnew(HBoxContainer);
	quick_row->add_theme_constant_override("separation", 6);
	quick_row->add_child(_make_quick_button(TTR("Mapa"), TTR("cria um mapa simples pra eu testar")));
	quick_row->add_child(_make_quick_button(TTR("Colisão"), TTR("adiciona colisão no cenário")));
	quick_row->add_child(_make_quick_button(TTR("Arma"), TTR("cria uma arma básica")));
	panel->add_child(quick_row);

	// Segunda fileira de atalhos.
	quick_row2 = memnew(HBoxContainer);
	quick_row2->add_theme_constant_override("separation", 6);
	quick_row2->add_child(_make_quick_button(TTR("HUD Mobile"), TTR("adiciona controles de celular: joystick pra andar e botões de pular, atirar e recarregar na tela")));
	quick_row2->add_child(_make_quick_button(TTR("Lobby"), TTR("cria um lobby de entrada com lista de jogadores e botão de jogar")));
	quick_row2->add_child(_make_quick_button(TTR("Ajuda"), "__HELP__"));
	panel->add_child(quick_row2);

	// Historico do chat. fit_content DESLIGADO de proposito: com fit_content
	// =true o balao crescia junto com o texto e empurrava a caixa de digitar
	// e os botoes pra fora da tela. Com altura fixa + rolagem interna tudo
	// embaixo fica sempre visivel.
	chat = memnew(RichTextLabel);
	chat->set_use_bbcode(true);
	chat->set_fit_content(false);
	chat->set_scroll_active(true);
	chat->set_scroll_follow(true);
	chat->set_custom_minimum_size(Size2(0, 220));
	chat->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	chat->add_theme_font_size_override("normal_font_size", 15);
	chat->add_theme_font_size_override("bold_font_size", 15);
	panel->add_child(chat);

	// Botoes de permissao (aparecem quando a NEX pede).
	perm_row = memnew(HBoxContainer);
	perm_row->add_theme_constant_override("separation", 6);
	btn_yes = memnew(Button);
	btn_yes->set_text(TTR("SIM, PODE FAZER"));
	btn_yes->set_custom_minimum_size(Size2(0, 44));
	btn_yes->add_theme_font_size_override("font_size", 14);
	btn_yes->add_theme_color_override("font_color", NEX_PURPLE_LIGHT);
	btn_yes->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	perm_row->add_child(btn_yes);
	btn_no = memnew(Button);
	btn_no->set_text(TTR("NÃO, SÓ ME EXPLICA"));
	btn_no->set_custom_minimum_size(Size2(0, 44));
	btn_no->add_theme_font_size_override("font_size", 14);
	btn_no->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	perm_row->add_child(btn_no);
	perm_row->hide();
	panel->add_child(perm_row);

	btn_yes->connect("pressed", callable_mp(this, &NexAIChatPlugin::_on_permission).bind(true));
	btn_no->connect("pressed", callable_mp(this, &NexAIChatPlugin::_on_permission).bind(false));

	// Campo de entrada + botao enviar (sempre visiveis).
	HBoxContainer *entry_row = memnew(HBoxContainer);
	entry_row->add_theme_constant_override("separation", 6);
	input = memnew(LineEdit);
	input->set_placeholder(TTR("Escreva aqui o que você quer..."));
	input->set_custom_minimum_size(Size2(0, 44));
	input->add_theme_font_size_override("font_size", 15);
	input->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	input->connect("text_submitted", callable_mp(this, &NexAIChatPlugin::_process_message));
	entry_row->add_child(input);

	Button *send = memnew(Button);
	send->set_text(TTR("Enviar"));
	send->set_custom_minimum_size(Size2(70, 44));
	send->add_theme_font_size_override("font_size", 15);
	send->connect("pressed", callable_mp(this, &NexAIChatPlugin::_process_message).bind(String()));
	entry_row->add_child(send);
	panel->add_child(entry_row);

	// Saldo de creditos (conta do dono = ilimitado).
	credits = memnew(Label);
	credits->set_text(TTR("Créditos: ilimitado"));
	credits->add_theme_color_override("font_color", NEX_PURPLE);
	credits->add_theme_font_size_override("font_size", 13);
	panel->add_child(credits);

	add_control_to_dock(DOCK_SLOT_RIGHT_UL, panel);

	_append_chat("NEX", TTR("Oi! Eu sou a NEX.\n\nToque num botão roxo (Mapa, Colisão, Arma, HUD Mobile, Lobby) ou escreva o que quiser na caixinha de baixo. Perguntas sobre o editor eu respondo na hora. Pedidos de criação eu faço AO VIVO na sua tela, passo a passo, depois que você autorizar."), NEX_PURPLE_LIGHT);
}

void NexAIChatPlugin::_append_chat(const String &p_who, const String &p_text, const Color &p_color) {
	String color_hex = p_color.to_html(false);
	chat->append_text("[color=#" + color_hex + "][b]" + p_who + ":[/b][/color] " + p_text + "\n\n");
}

void NexAIChatPlugin::_show_tutorial() {
	_append_chat("NEX", TTR(
			"Como eu funciono:\n\n"
			"1) Peça algo: toque num botão roxo ou escreva (ex: \"cria um mapa pvp\", \"faz uma arma\", \"adiciona joystick\").\n\n"
			"2) Eu peço autorização. Toque em \"SIM, PODE FAZER\" e eu crio tudo AO VIVO na sua cena, narrando cada passo aqui no chat.\n\n"
			"3) Perguntas sobre o editor (como salvar, rodar, exportar APK) eu respondo direto, sem precisar de permissão.\n\n"
			"Lembrete: eu não copio jogos de outras empresas (Free Fire, Roblox etc), mas crio originais com a sua cara."),
			NEX_PURPLE_LIGHT);
}

void NexAIChatPlugin::_queue_live(int p_kind, const String &p_narration) {
	live_kinds.push_back(p_kind);
	live_narrations.push_back(p_narration);
	live_total++;
}

Node *NexAIChatPlugin::_scene_root_3d_check() {
	Node *root = EditorNode::get_singleton()->get_edited_scene();
	if (root == nullptr) {
		return nullptr;
	}
	return root;
}

void NexAIChatPlugin::_answer_question(const String &p_low) {
	String low = p_low;
	String answer;

	if (low.contains("salvar") || low.contains("save")) {
		answer = TTR("Pra salvar sua cena: toque no menu Projeto (lá em cima) e depois em \"Salvar Cena\". Se não salvar, o que eu criar se perde quando fechar o editor, então salva sempre depois dos meus trabalhos!");
	} else if (low.contains("rodar") || low.contains("testar") || low.contains("play") || low.contains("executar") || low.contains("ver o jogo")) {
		answer = TTR("Pra ver o jogo rodando: toque no triângulo (play) na barra de cima. O jogo abre na hora. Pra voltar pro editor, toque no quadrado (parar).");
	} else if (low.contains("exportar") || low.contains("apk") || low.contains("instalar")) {
		answer = TTR("Pra gerar o APK instalável: menu Projeto > Exportar > Adicionar preset Android > Exportar projeto. Aí é só instalar o APK no celular.");
	} else if (low.contains("2d") || low.contains("3d") || (low.contains("cena") && low.contains("diferen"))) {
		answer = TTR("As abas do meio em cima: Cena 2D é plano (lado a lado), Cena 3D tem profundidade (tipo battle royale) e Interface de Usuário é a HUD. Pra jogo com mapa e tiro, use a Cena 3D.");
	} else if (low.contains("script") || low.contains("código") || low.contains("codigo")) {
		answer = TTR("Script é o código do jogo (GDScript). Selecione um nó na cena e toque no ícone de rolo preso (anexar script) que o editor cria o código dele. Numa versão futura eu escrevo os scripts pra você.");
	} else if (low.contains("quem") || low.contains("você é") || low.contains("voce e")) {
		answer = TTR("Eu sou a NEX, a ajudante que mora dentro da Nexus Engine. Eu crio coisas na sua cena ao vivo, explico o editor e jogo junto com você no seu jogo.");
	} else if (low.contains("crédito") || low.contains("credito") || low.contains("custa")) {
		answer = TTR("Créditos: pra você, ilimitado, sempre. A engine é sua.");
	} else if (low.contains("multiplayer") || low.contains("servidor") || low.contains("online")) {
		answer = TTR("Multiplayer é a última etapa e tá no plano. O caminho é: mapa e colisão funcionando, depois HUD mobile, animações, e aí o servidor online. Um passo de cada vez.");
	} else if (low.contains("renomear") || low.contains("nome")) {
		answer = TTR("Pra renomear um nó: toque duas vezes no nome dele na árvore da cena (canto esquerdo) e escreva o novo nome.");
	} else {
		answer = TTR("Boa pergunta! Nessa versão eu domino o básico do editor: criar cena (botão + e escolher 2D ou 3D), salvar (menu Projeto), rodar (triângulo), exportar APK (Projeto > Exportar) e criar coisas ao vivo (mapa, colisão, arma, HUD mobile, lobby). Perguntas mais avançadas chegam com meu cérebro completo na próxima versão. Enquanto isso, me pede pra CRIAR algo!");
	}
	_append_chat("NEX", answer, NEX_PURPLE_LIGHT);
}

void NexAIChatPlugin::_classify_and_ask(const String &p_low) {
	// Classifica a intencao de criacao e pede permissao.
	if (p_low.contains("mapa") || p_low.contains("mundo") || p_low.contains("terreno")) {
		pending_action = ACTION_MAP;
		_append_chat("NEX", TTR("Mapa PvP! Vou criar na sua frente: o chão com colisão, as 4 paredes e a luz do sol. Tudo aparece na Cena 3D ao vivo. Pode ser?"), NEX_PURPLE_LIGHT);
	} else if (p_low.contains("colis") || p_low.contains("parede invis") || p_low.contains("bloqueio")) {
		pending_action = ACTION_COLLISION;
		_append_chat("NEX", TTR("Colisão! Vou adicionar uma caixa sólida na cena (nada atravessa ela). Pode ser?"), NEX_PURPLE_LIGHT);
	} else if (p_low.contains("arma") || p_low.contains("tiro") || p_low.contains("weapon") || p_low.contains("gun")) {
		pending_action = ACTION_WEAPON;
		_append_chat("NEX", TTR("Arma! Vou montar o corpo e o cano de uma arma básica na cena, ao vivo. Pode ser?"), NEX_PURPLE_LIGHT);
	} else if (p_low.contains("joystick") || p_low.contains("hud") || p_low.contains("celular") || p_low.contains("mobile") || p_low.contains("controle")) {
		pending_action = ACTION_HUD;
		_append_chat("NEX", TTR("HUD Mobile! Vou montar o joystick na esquerda e os botões PULAR, ATIRAR e RECARREGAR na direita. Ele aparece na tela quando o jogo roda (play). Pode ser?"), NEX_PURPLE_LIGHT);
	} else if (p_low.contains("lobby") || p_low.contains("menu inicial") || p_low.contains("tela inicial") || p_low.contains("entrada")) {
		pending_action = ACTION_LOBBY;
		_append_chat("NEX", TTR("Lobby! Vou montar a tela de entrada: fundo, título e o botão JOGAR. Pode ser?"), NEX_PURPLE_LIGHT);
	} else {
		pending_action = ACTION_GENERIC;
		_append_chat("NEX", vformat(TTR("Entendi! Vou criar \"%s\" como um objeto 3D na sua cena, ao vivo. Pode ser?"), pending_text), NEX_PURPLE_LIGHT);
	}
	perm_row->show();
}

// helper pequena pra nao virar if gigante (saudacoes curtas com ponto/exclamacao).
static bool _greet_word(const String &p_low) {
	static const char *g[] = { "oi!", "ola!", "olá!", "eai!", "opa", "e aí" };
	for (const char *w : g) {
		if (p_low == String(w) || p_low.begins_with(String(w) + " ")) {
			return true;
		}
	}
	return false;
}

void NexAIChatPlugin::_process_message(const String &p_text) {
	String txt = p_text.strip_edges();
	if (txt.is_empty()) {
		txt = input->get_text().strip_edges();
	}
	if (txt.is_empty()) {
		return;
	}
	input->set_text("");
	_append_chat(TTR("Você"), txt, Color(1, 1, 1, 1));
	pending_text = txt;
	pending_action = ACTION_NONE;

	String low = txt.to_lower();

	// TRAVA ANTI-COPIA (secao 6 do NEX_AI.md).
	static const char *forbidden[] = {
		"free fire", "freefire", "pubg", "valorant", "cs2", "counter strike",
		"call of duty", "gta", "fortnite", "minecraft", "roblox", "among us",
		"fall guys", "brawl stars", "clash royale", "league of legends",
		"stardew valley", "terraria", "subway surfers", "candy crush",
		"copia", "copiar", "clona", "clonar", "igualzinho"
	};
	for (const char *word : forbidden) {
		if (low.contains(String(word))) {
			_append_chat("NEX", TTR("Não posso copiar jogos de outras empresas pra te proteger de processo, mas posso criar um jogo AINDA MELHOR com sua cara. Bora criar um original do mesmo estilo?"), NEX_PURPLE_LIGHT);
			return;
		}
	}

	// Saudacoes.
	if (low == "oi" || low == "ola" || low == "olá" || low == "eai" || low == "e ai" ||
			_greet_word(low) || low.contains("bom dia") || low.contains("boa tarde") || low.contains("boa noite") || low.contains("tudo bem")) {
		_append_chat("NEX", TTR("Opa! Tô on. Me pede pra criar (mapa, colisão, arma, HUD mobile, lobby) ou pergunta qualquer coisa sobre o editor que eu explico."), NEX_PURPLE_LIGHT);
		return;
	}

	// Perguntas: responde direto, sem permissao.
	bool is_question = txt.contains("?") || low.begins_with("como") || low.begins_with("oque") ||
			low.begins_with("o que") || low.begins_with("qual") || low.begins_with("quem") ||
			low.begins_with("quando") || low.begins_with("onde") || low.begins_with("por que") ||
			low.begins_with("porque") || low.begins_with("pq") || low.begins_with("quanto") ||
			low.contains(" ajuda");
	if (is_question) {
		_answer_question(low);
		return;
	}

	// Pedidos de criacao: pede permissao e roda o modo ao vivo.
	_classify_and_ask(low);
}

void NexAIChatPlugin::_on_permission(bool p_allow) {
	perm_row->hide();
	if (!p_allow) {
		_append_chat("NEX", TTR("Tranquilo! Aí eu te explico na mão: selecione o nó e use o botão + da árvore da cena pra adicionar filhos. Qualquer coisa me chama que eu crio pra você depois."), NEX_PURPLE_LIGHT);
		pending_action = ACTION_NONE;
		return;
	}
	if (pending_action == ACTION_NONE) {
		return;
	}
	_start_live();
}

void NexAIChatPlugin::_start_live() {
	Node *root = _scene_root_3d_check();
	if (root == nullptr) {
		_append_chat("NEX", TTR("Ei, ainda não tem cena aberta (ou tá vazia). Toque no + pra criar uma \"Cena 3D\" e me pede de novo que eu faço na hora."), NEX_PURPLE_LIGHT);
		pending_action = ACTION_NONE;
		return;
	}

	// Acoes 3D precisam de raiz Node3D.
	if (pending_action == ACTION_MAP || pending_action == ACTION_COLLISION || pending_action == ACTION_WEAPON || pending_action == ACTION_GENERIC) {
		if (Object::cast_to<Node3D>(root) == nullptr) {
			_append_chat("NEX", TTR("Sua cena atual não é 3D. Toque no + pra criar uma \"Cena 3D\" (a que tem profundidade) e me pede de novo. HUD e Lobby eu consigo criar aqui mesmo."), NEX_PURPLE_LIGHT);
			pending_action = ACTION_NONE;
			return;
		}
	}

	live_narrations.clear();
	live_kinds.clear();
	live_index = 0;
	live_total = 0;

	switch (pending_action) {
		case ACTION_MAP:
			_append_chat("NEX", TTR("Fechado! Modo ao vivo ligado. Olha a cena 3D (perspectiva) que eu vou montando..."), NEX_PURPLE_LIGHT);
			_queue_live(STEP_GROUND, TTR("Criando o chão do mapa (20x20) com colisão..."));
			_queue_live(STEP_WALLS, TTR("Levantando as 4 paredes ao redor da arena..."));
			_queue_live(STEP_LIGHT, TTR("Acendendo a luz do sol..."));
			break;
		case ACTION_COLLISION:
			_append_chat("NEX", TTR("Fechado! Modo ao vivo ligado..."), NEX_PURPLE_LIGHT);
			_queue_live(STEP_COLLISION, TTR("Adicionando a caixa de colisão sólida..."));
			break;
		case ACTION_WEAPON:
			_append_chat("NEX", TTR("Fechado! Modo ao vivo ligado..."), NEX_PURPLE_LIGHT);
			_queue_live(STEP_GUN, TTR("Montando o corpo e o cano da arma..."));
			break;
		case ACTION_HUD:
			_append_chat("NEX", TTR("Fechado! Modo ao vivo ligado..."), NEX_PURPLE_LIGHT);
			_queue_live(STEP_HUD, TTR("Montando o HUD mobile: joystick + botões PULAR, ATIRAR e RECARREGAR..."));
			break;
		case ACTION_LOBBY:
			_append_chat("NEX", TTR("Fechado! Modo ao vivo ligado..."), NEX_PURPLE_LIGHT);
			_queue_live(STEP_LOBBY, TTR("Montando o lobby: fundo, título e botão JOGAR..."));
			break;
		default:
			_append_chat("NEX", TTR("Fechado! Modo ao vivo ligado..."), NEX_PURPLE_LIGHT);
			_queue_live(STEP_GENERIC, TTR("Criando o objeto na cena..."));
			break;
	}
	_live_next_step();
}

void NexAIChatPlugin::_live_next_step() {
	if (live_index >= live_total) {
		// Fim: mensagem de conclusao por acao.
		switch (pending_action) {
			case ACTION_MAP:
				_append_chat("NEX", TTR("PRONTINHO! Mapa PvP criado: chão com colisão, 4 paredes e luz do sol. Vê a aba da cena 3D (perspectiva) e a árvore de nós. Salva no menu Projeto pra não perder!"), NEX_PURPLE_LIGHT);
				break;
			case ACTION_COLLISION:
				_append_chat("NEX", TTR("PRONTINHO! Colisão sólida na cena. Nada atravessa mais essa caixa."), NEX_PURPLE_LIGHT);
				break;
			case ACTION_WEAPON:
				_append_chat("NEX", TTR("PRONTINHO! Arma criada e selecionada. Foi só o visual; o sistema de tiro (dano, munição) vem na próxima versão."), NEX_PURPLE_LIGHT);
				break;
			case ACTION_HUD:
				_append_chat("NEX", TTR("PRONTINHO! HUD mobile montado (joystick + PULAR, ATIRAR, RECARREGAR). Ele aparece quando você RODA o jogo (triângulo), porque controle de toque é do jogo, não do editor."), NEX_PURPLE_LIGHT);
				break;
			case ACTION_LOBBY:
				_append_chat("NEX", TTR("PRONTINHO! Lobby montado: fundo, título e botão JOGAR. Roda o jogo (play) pra ver ele na tela."), NEX_PURPLE_LIGHT);
				break;
			default:
				_append_chat("NEX", TTR("PRONTINHO! Objeto criado na sua cena."), NEX_PURPLE_LIGHT);
				break;
		}
		pending_action = ACTION_NONE;
		live_total = 0;
		return;
	}

	int kind = live_kinds[live_index];
	_append_chat("NEX", live_narrations[live_index], NEX_PURPLE_LIGHT);

	Node *root = EditorNode::get_singleton()->get_edited_scene();
	Node *mapa = nullptr;
	if (root != nullptr && root->has_node("Mapa_PvP")) {
		mapa = root->get_node("Mapa_PvP");
	}

	switch (kind) {
		case STEP_GROUND: {
			Node3D *map = memnew(Node3D);
			map->set_name("Mapa_PvP");
			// Chao solido com mesh + colisao.
			StaticBody3D *chao = memnew(StaticBody3D);
			chao->set_name("Chao");
			chao->set_position(Vector3(0, -0.25f, 0));
			MeshInstance3D *chao_mesh = memnew(MeshInstance3D);
			chao_mesh->set_name("Chao_Visual");
			Ref<BoxMesh> chao_box;
			chao_box.instantiate();
			chao_box->set_size(Vector3(20, 0.5f, 20));
			chao_mesh->set_mesh(chao_box);
			chao_mesh->set_material_override(_nex_mat(0.22f, 0.15f, 0.32f));
			chao->add_child(chao_mesh);
			CollisionShape3D *chao_cs = memnew(CollisionShape3D);
			chao_cs->set_name("Chao_Colisao");
			Ref<BoxShape3D> chao_shape;
			chao_shape.instantiate();
			chao_shape->set_size(Vector3(20, 0.5f, 20));
			chao_cs->set_shape(chao_shape);
			chao->add_child(chao_cs);
			map->add_child(chao);
			_add_node_live(root, map, TTR("NEX: criar mapa (chao)"));
			break;
		}
		case STEP_WALLS: {
			Node *parent = mapa != nullptr ? mapa : root;
			Node3D *paredes = memnew(Node3D);
			paredes->set_name("Paredes");
			static const Vector3 sizes[4] = {
				Vector3(20, 2, 0.5f), Vector3(20, 2, 0.5f),
				Vector3(0.5f, 2, 20), Vector3(0.5f, 2, 20)
			};
			static const Vector3 positions[4] = {
				Vector3(0, 1, 10), Vector3(0, 1, -10),
				Vector3(10, 1, 0), Vector3(-10, 1, 0)
			};
			static const char *wall_names[4] = { "Parede_Frente", "Parede_Tras", "Parede_Direita", "Parede_Esquerda" };
			for (int i = 0; i < 4; i++) {
				StaticBody3D *wall = memnew(StaticBody3D);
				wall->set_name(wall_names[i]);
				wall->set_position(positions[i]);
				MeshInstance3D *wm = memnew(MeshInstance3D);
				wm->set_name("Visual");
				Ref<BoxMesh> wb;
				wb.instantiate();
				wb->set_size(sizes[i]);
				wm->set_mesh(wb);
				wm->set_material_override(_nex_mat(0.45f, 0.28f, 0.65f));
				wall->add_child(wm);
				CollisionShape3D *wcs = memnew(CollisionShape3D);
				wcs->set_name("Colisao");
				Ref<BoxShape3D> wsh;
				wsh.instantiate();
				wsh->set_size(sizes[i]);
				wcs->set_shape(wsh);
				wall->add_child(wcs);
				paredes->add_child(wall);
			}
			_add_node_live(parent, paredes, TTR("NEX: criar paredes do mapa"));
			break;
		}
		case STEP_LIGHT: {
			Node *parent = mapa != nullptr ? mapa : root;
			DirectionalLight3D *sol = memnew(DirectionalLight3D);
			sol->set_name("Luz_Sol");
			sol->set_rotation_degrees(Vector3(-45, -30, 0));
			_add_node_live(parent, sol, TTR("NEX: acender luz do sol"));
			break;
		}
		case STEP_COLLISION: {
			StaticBody3D *col = memnew(StaticBody3D);
			col->set_name("Colisao");
			col->set_position(Vector3(0, 1, 0));
			CollisionShape3D *cs = memnew(CollisionShape3D);
			cs->set_name("Formato");
			Ref<BoxShape3D> sh;
			sh.instantiate();
			sh->set_size(Vector3(2, 2, 2));
			cs->set_shape(sh);
			col->add_child(cs);
			_add_node_live(root, col, TTR("NEX: adicionar colisao"));
			break;
		}
		case STEP_GUN: {
			Node3D *arma = memnew(Node3D);
			arma->set_name("Arma");
			arma->set_position(Vector3(1, 1, 0));
			MeshInstance3D *corpo = memnew(MeshInstance3D);
			corpo->set_name("Corpo");
			Ref<BoxMesh> cb;
			cb.instantiate();
			cb->set_size(Vector3(0.12f, 0.18f, 0.55f));
			corpo->set_mesh(cb);
			corpo->set_material_override(_nex_mat(0.2f, 0.12f, 0.3f));
			arma->add_child(corpo);
			MeshInstance3D *cano = memnew(MeshInstance3D);
			cano->set_name("Cano");
			cano->set_position(Vector3(0, 0.05f, 0.45f));
			Ref<BoxMesh> kb;
			kb.instantiate();
			kb->set_size(Vector3(0.05f, 0.05f, 0.4f));
			cano->set_mesh(kb);
			cano->set_material_override(_nex_mat(0.12f, 0.08f, 0.18f));
			arma->add_child(cano);
			_add_node_live(root, arma, TTR("NEX: criar arma"));
			break;
		}
		case STEP_HUD: {
			CanvasLayer *hud = memnew(CanvasLayer);
			hud->set_name("HUD_Mobile");
			// Base do joystick (esquerda embaixo).
			ColorRect *base = memnew(ColorRect);
			base->set_name("Base_Joystick");
			base->set_color(Color(0.24f, 0.12f, 0.36f, 0.55f));
			base->set_anchors_and_offsets_preset(Control::PRESET_BOTTOM_LEFT, Control::PRESET_MODE_MINSIZE);
			base->set_size(Size2(120, 120));
			base->set_position(Point2(30, -170));
			hud->add_child(base);
			// Bola do joystick.
			ColorRect *bola = memnew(ColorRect);
			bola->set_name("Bola_Joystick");
			bola->set_color(Color(0.66f, 0.33f, 0.97f, 0.9f));
			bola->set_anchors_and_offsets_preset(Control::PRESET_BOTTOM_LEFT, Control::PRESET_MODE_MINSIZE);
			bola->set_size(Size2(50, 50));
			bola->set_position(Point2(65, -135));
			hud->add_child(bola);
			// Botoes de acao (direita embaixo).
			static const char *btn_names[3] = { "Btn_Pular", "Btn_Atirar", "Btn_Recarregar" };
			static const char *btn_texts[3] = { "PULAR", "ATIRAR", "RECARR." };
			static const Point2 btn_pos[3] = { Point2(-210, -110), Point2(-100, -120), Point2(-210, -230) };
			static const Size2 btn_size[3] = { Size2(90, 90), Size2(90, 90), Size2(90, 70) };
			for (int i = 0; i < 3; i++) {
				Button *btn = memnew(Button);
				btn->set_name(btn_names[i]);
				btn->set_text(btn_texts[i]);
				btn->add_theme_font_size_override("font_size", 12);
				btn->set_anchors_and_offsets_preset(Control::PRESET_BOTTOM_RIGHT, Control::PRESET_MODE_MINSIZE);
				btn->set_size(btn_size[i]);
				btn->set_position(btn_pos[i]);
				hud->add_child(btn);
			}
			_add_node_live(root, hud, TTR("NEX: montar HUD mobile"));
			break;
		}
		case STEP_LOBBY: {
			CanvasLayer *lobby = memnew(CanvasLayer);
			lobby->set_name("Lobby");
			ColorRect *fundo = memnew(ColorRect);
			fundo->set_name("Fundo");
			fundo->set_color(Color(0.12f, 0.08f, 0.2f, 1));
			fundo->set_anchors_and_offsets_preset(Control::PRESET_FULL_RECT, Control::PRESET_MODE_MINSIZE);
			lobby->add_child(fundo);
			Label *titulo = memnew(Label);
			titulo->set_name("Titulo");
			titulo->set_text(TTR("LOBBY"));
			titulo->add_theme_font_size_override("font_size", 44);
			titulo->add_theme_color_override("font_color", NEX_PURPLE_LIGHT);
			titulo->set_anchors_and_offsets_preset(Control::PRESET_CENTER_TOP, Control::PRESET_MODE_MINSIZE);
			titulo->set_size(Size2(200, 60));
			titulo->set_position(Point2(-100, 60));
			lobby->add_child(titulo);
			Button *jogar = memnew(Button);
			jogar->set_name("Btn_Jogar");
			jogar->set_text(TTR("JOGAR"));
			jogar->add_theme_font_size_override("font_size", 20);
			jogar->set_anchors_and_offsets_preset(Control::PRESET_CENTER, Control::PRESET_MODE_MINSIZE);
			jogar->set_size(Size2(180, 64));
			jogar->set_position(Point2(-90, -32));
			lobby->add_child(jogar);
			_add_node_live(root, lobby, TTR("NEX: montar lobby"));
			break;
		}
		default: { // STEP_GENERIC
			MeshInstance3D *obj = memnew(MeshInstance3D);
			obj->set_name("Objeto");
			Ref<BoxMesh> gb;
			gb.instantiate();
			gb->set_size(Vector3(1, 1, 1));
			obj->set_mesh(gb);
			obj->set_material_override(_nex_mat(0.66f, 0.33f, 0.97f));
			_add_node_live(root, obj, TTR("NEX: criar objeto"));
			break;
		}
	}

	live_index++;
	if (live_index < live_total) {
		// Pausa entre passos pra dar o efeito "fazendo ao vivo".
		get_tree()->create_timer(0.9f)->connect("timeout", callable_mp(this, &NexAIChatPlugin::_live_next_step));
	} else {
		get_tree()->create_timer(0.9f)->connect("timeout", callable_mp(this, &NexAIChatPlugin::_live_next_step));
	}
}

bool NexAIChatPlugin::_add_node_live(Node *p_parent, Node *p_child, const String &p_action_name) {
	Node *edited_scene = EditorNode::get_singleton()->get_edited_scene();
	if (p_parent == nullptr || p_child == nullptr) {
		return false;
	}
	p_parent->validate_child_name(p_child);
	EditorUndoRedoManager *undo_redo = EditorUndoRedoManager::get_singleton();
	EditorSelection *sel = EditorNode::get_singleton()->get_editor_selection();
	undo_redo->create_action_for_history(p_action_name, EditorNode::get_editor_data().get_current_edited_scene_history_id());
	undo_redo->add_do_method(sel, "clear");
	undo_redo->add_do_method(p_parent, "add_child", p_child, true);
	undo_redo->add_do_method(p_child, "set_owner", edited_scene);
	undo_redo->add_do_method(sel, "add_node", p_child);
	undo_redo->add_do_reference(p_child);
	undo_redo->add_undo_method(p_parent, "remove_child", p_child);
	undo_redo->commit_action();
	return true;
}

void NexAIChatPlugin::_notification(int p_what) {
	// Nada por enquanto; mantido pra extensões futuras.
}
