/**************************************************************************/
/*  nex_ai_chat_plugin.cpp                                                */
/*  NEX AI — chat dock lateral da Nexus Engine                             */
/*  Spec: NEX_AI.md (documento oficial do módulo NEX)                      */
/*  v0.3 — MODO AO VIVO + RECEITAS VISUAIS COMPOSTAS:                      */
/*  modelos de verdade (nao tudo quadrado), cores, tamanhos e               */
/*  quantidades entendidos direto do pedido do dev.                        */
/**************************************************************************/

#include "editor/plugins/nex_ai_chat_plugin.h"

#include "core/math/color.h"
#include "core/object/callable_mp.h"
#include "core/string/node_path.h"
#include "core/string/ustring.h"
#include "core/variant/variant.h"
#include "editor/editor_data.h"
#include "editor/editor_node.h"
#include "editor/editor_undo_redo_manager.h"
#include "editor/plugins/editor_plugin.h"
#include "scene/3d/light_3d.h"
#include "scene/3d/mesh_instance_3d.h"
#include "scene/3d/node_3d.h"
#include "scene/3d/physics/collision_shape_3d.h"
#include "scene/3d/physics/static_body_3d.h"
#include "scene/3d/physics/character_body_3d.h"
#include "scene/3d/world_environment.h"
#include "scene/gui/box_container.h"
#include "scene/gui/button.h"
#include "scene/gui/color_rect.h"
#include "scene/gui/control.h"
#include "scene/gui/label.h"
#include "scene/gui/line_edit.h"
#include "scene/gui/rich_text_label.h"
#include "scene/main/canvas_layer.h"
#include "scene/main/node.h"
#include "scene/main/scene_tree.h"
#include "scene/resources/3d/box_shape_3d.h"
#include "scene/resources/3d/capsule_shape_3d.h"
#include "scene/resources/3d/cylinder_shape_3d.h"
#include "scene/resources/3d/primitive_meshes.h"
#include "scene/resources/3d/sky_material.h"
#include "scene/resources/environment.h"
#include "scene/resources/sky.h"
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
	STEP_LIGHT_ENV,
	STEP_DECOR,
	STEP_COLLISION,
	STEP_GUN,
	STEP_HUD,
	STEP_LOBBY,
	STEP_TREE,
	STEP_CHAR,
	STEP_HOUSE,
	STEP_CAR,
	STEP_RAMP,
	STEP_CRYSTAL,
	STEP_COIN,
	STEP_PLATFORM,
	STEP_GENERIC,
};

// ---------------- helpers de material --------------------------------------

static Ref<StandardMaterial3D> _mat(const Color &p_col, float p_metal = 0.0f, bool p_emissive = false, const Color &p_emission = Color()) {
	Ref<StandardMaterial3D> mat;
	mat.instantiate();
	mat->set_albedo(p_col);
	mat->set_roughness(0.45f);
	mat->set_metallic(p_metal);
	if (p_emissive) {
		mat->set_feature(StandardMaterial3D::FEATURE_EMISSION, true);
		mat->set_emission(p_emission);
	}
	return mat;
}

// Adiciona um primitivo (esfera, cilindro, capsula etc) como filho visual.
static MeshInstance3D *_add_prim(Node3D *p_parent, const String &p_name, Ref<PrimitiveMesh> p_mesh,
		const Vector3 &p_pos, const Color &p_col, float p_metal = 0.0f, bool p_emissive = false) {
	MeshInstance3D *m = memnew(MeshInstance3D);
	m->set_name(p_name);
	m->set_mesh(p_mesh);
	m->set_position(p_pos);
	m->set_material_override(_mat(p_col, p_metal, p_emissive, p_col));
	p_parent->add_child(m);
	return m;
}

// Tira acento pra comparar pedido do dev sem se preocupar com acentuacao.
static String _unaccent(const String &p_s) {
	String s = p_s;
	static const char *pairs[][2] = {
		{ "á", "a" }, { "à", "a" }, { "â", "a" }, { "ã", "a" },
		{ "é", "e" }, { "ê", "e" }, { "í", "i" },
		{ "ó", "o" }, { "ô", "o" }, { "õ", "o" },
		{ "ú", "u" }, { "ç", "c" }
	};
	for (unsigned int i = 0; i < sizeof(pairs) / sizeof(pairs[0]); i++) {
		s = s.replace(String::utf8(pairs[i][0]), String(pairs[i][1]));
	}
	return s;
}

// Cor pedida em palavras (preto, verde, dourado...).
static bool _color_from_text(const String &p_low, Color &r_col) {
	static const char *words[] = { "preto", "branco", "vermelho", "azul", "verde",
		"amarelo", "roxo", "rosa", "laranja", "cinza", "dourado", "marrom", "ciano" };
	static const Color cols[] = {
		Color(0.12f, 0.12f, 0.12f), Color(0.92f, 0.92f, 0.92f),
		Color(0.85f, 0.15f, 0.15f), Color(0.15f, 0.35f, 0.9f),
		Color(0.2f, 0.75f, 0.3f), Color(0.95f, 0.85f, 0.1f),
		Color(0.66f, 0.33f, 0.97f), Color(0.98f, 0.45f, 0.75f),
		Color(0.95f, 0.55f, 0.1f), Color(0.45f, 0.45f, 0.5f),
		Color(1.0f, 0.8f, 0.2f), Color(0.5f, 0.33f, 0.15f),
		Color(0.1f, 0.85f, 0.85f)
	};
	for (int i = 0; i < 13; i++) {
		if (p_low.contains(String(words[i]))) {
			r_col = cols[i];
			return true;
		}
	}
	return false;
}

// Quantidade pedida (digito ou por extenso, 1 a 10).
static int _count_from_text(const String &p_low) {
	static const char *num_words[] = { "uma", "um ", "dois", "duas", "tres", "quatro",
		"cinco", "seis", "sete", "oito", "nove", "dez" };
	for (int i = 1; i <= 10; i++) {
		if (p_low.contains(String::num_int64(i))) {
			return i;
		}
	}
	for (int i = 1; i <= 10; i++) {
		if (p_low.contains(String(num_words[i]))) {
			return i;
		}
	}
	return 1;
}

// Tamanho pedido.
static float _scale_from_text(const String &p_low) {
	if (p_low.contains("gigante") || p_low.contains("enorme")) {
		return 3.0f;
	}
	if (p_low.contains("grande") || p_low.contains("maior")) {
		return 2.0f;
	}
	if (p_low.contains("pequeno") || p_low.contains("menor") || p_low.contains("mini")) {
		return 0.5f;
	}
	return 1.0f;
}

// ---------------- receitas visuais (modelos compostos) ---------------------

// Arvore: tronco cilindro + copa esfera.
static Node3D *_recipe_tree(const Color &p_copa) {
	Node3D *tree = memnew(Node3D);
	tree->set_name("Arvore");
	Ref<CylinderMesh> tronco;
	tronco.instantiate();
	tronco->set_top_radius(0.18f);
	tronco->set_bottom_radius(0.28f);
	tronco->set_height(1.6f);
	_add_prim(tree, "Tronco", tronco, Vector3(0, 0.8f, 0), Color(0.45f, 0.3f, 0.12f));
	Ref<SphereMesh> copa;
	copa.instantiate();
	copa->set_radius(0.95f);
	copa->set_height(1.6f);
	_add_prim(tree, "Copa", copa, Vector3(0, 1.9f, 0), p_copa);
	return tree;
}

// Personagem: corpo capsula + colisao + cabeca esfera + 2 bracos.
static CharacterBody3D *_recipe_char(const Color &p_cor) {
	CharacterBody3D *chr = memnew(CharacterBody3D);
	chr->set_name("Personagem");
	Ref<CapsuleShape3D> csh;
	csh.instantiate();
	csh->set_radius(0.35f);
	csh->set_height(1.1f);
	CollisionShape3D *cs = memnew(CollisionShape3D);
	cs->set_name("Colisao");
	cs->set_shape(csh);
	cs->set_position(Vector3(0, 0.55f, 0));
	chr->add_child(cs);
	Ref<CapsuleMesh> corpo;
	corpo.instantiate();
	corpo->set_radius(0.35f);
	corpo->set_height(0.9f);
	_add_prim(chr, "Corpo", corpo, Vector3(0, 0.55f, 0), p_cor);
	Ref<SphereMesh> cabeca;
	cabeca.instantiate();
	cabeca->set_radius(0.28f);
	cabeca->set_height(0.56f);
	_add_prim(chr, "Cabeca", cabeca, Vector3(0, 1.25f, 0), Color(0.95f, 0.8f, 0.65f));
	// braco esquerdo
	Ref<CapsuleMesh> be;
	be.instantiate();
	be->set_radius(0.1f);
	be->set_height(0.6f);
	_add_prim(chr, "Braco_Esq", be, Vector3(-0.48f, 0.55f, 0), p_cor);
	// braco direito
	Ref<CapsuleMesh> bd;
	bd.instantiate();
	bd->set_radius(0.1f);
	bd->set_height(0.6f);
	_add_prim(chr, "Braco_Dir", bd, Vector3(0.48f, 0.55f, 0), p_cor);
	// pernas
	Ref<CapsuleMesh> pe;
	pe.instantiate();
	pe->set_radius(0.12f);
	pe->set_height(0.5f);
	_add_prim(chr, "Perna_Esq", pe, Vector3(-0.16f, 0.25f, 0), Color(0.15f, 0.15f, 0.2f));
	Ref<CapsuleMesh> pd;
	pd.instantiate();
	pd->set_radius(0.12f);
	pd->set_height(0.5f);
	_add_prim(chr, "Perna_Dir", pd, Vector3(0.16f, 0.25f, 0), Color(0.15f, 0.15f, 0.2f));
	return chr;
}

// Casa: chao + 4 paredes + porta + 2 janelas + telhado.
static Node3D *_recipe_house(const Color &p_parede) {
	Node3D *casa = memnew(Node3D);
	casa->set_name("Casa");
	// chao
	Ref<BoxMesh> chao;
	chao.instantiate();
	chao->set_size(Vector3(4, 0.2f, 4));
	_add_prim(casa, "Chao", chao, Vector3(0, 0.1f, 0), Color(0.4f, 0.4f, 0.45f));
	// paredes
	static const Vector3 wall_pos[4] = { Vector3(0, 1.45f, 2), Vector3(0, 1.45f, -2), Vector3(2, 1.45f, 0), Vector3(-2, 1.45f, 0) };
	static const Vector3 wall_size[4] = { Vector3(4, 2.7f, 0.2f), Vector3(4, 2.7f, 0.2f), Vector3(0.2f, 2.7f, 4), Vector3(0.2f, 2.7f, 4) };
	static const char *wall_names[4] = { "Parede_Frente", "Parede_Tras", "Parede_Direita", "Parede_Esquerda" };
	for (int i = 0; i < 4; i++) {
		Ref<BoxMesh> w;
		w.instantiate();
		w->set_size(wall_size[i]);
		_add_prim(casa, wall_names[i], w, wall_pos[i], p_parede);
	}
	// porta
	Ref<BoxMesh> porta;
	porta.instantiate();
	porta->set_size(Vector3(0.9f, 1.7f, 0.06f));
	_add_prim(casa, "Porta", porta, Vector3(0, 0.95f, 2.08f), Color(0.35f, 0.2f, 0.1f));
	// janelas (brilhando)
	Ref<BoxMesh> jan;
	jan.instantiate();
	jan->set_size(Vector3(0.06f, 0.7f, 0.9f));
	_add_prim(casa, "Janela_Dir", jan, Vector3(2.08f, 1.7f, 1), Color(1, 1, 0.6f), 0.0f, true);
	_add_prim(casa, "Janela_Esq", jan, Vector3(-2.08f, 1.7f, -1), Color(1, 1, 0.6f), 0.0f, true);
	// telhado
	Ref<PrismMesh> telhado;
	telhado.instantiate();
	telhado->set_size(Vector3(4.6f, 1.4f, 4.6f));
	_add_prim(casa, "Telhado", telhado, Vector3(0, 3.4f, 0), Color(0.7f, 0.15f, 0.15f));
	return casa;
}

// Carro: corpo + cabine + 4 rodas.
static Node3D *_recipe_car(const Color &p_cor) {
	Node3D *carro = memnew(Node3D);
	carro->set_name("Carro");
	Ref<BoxMesh> corpo;
	corpo.instantiate();
	corpo->set_size(Vector3(1.6f, 0.45f, 3.0f));
	_add_prim(carro, "Corpo", corpo, Vector3(0, 0.55f, 0), p_cor, 0.7f);
	Ref<BoxMesh> cabine;
	cabine.instantiate();
	cabine->set_size(Vector3(1.4f, 0.5f, 1.4f));
	_add_prim(carro, "Cabine", cabine, Vector3(0, 1.0f, -0.2f), Color(0.1f, 0.1f, 0.15f, 0.9f), 0.4f);
	Ref<CylinderMesh> roda;
	roda.instantiate();
	roda->set_top_radius(0.3f);
	roda->set_bottom_radius(0.3f);
	roda->set_height(0.22f);
	static const Vector3 wheel_pos[4] = {
		Vector3(-0.85f, 0.3f, 1.0f), Vector3(0.85f, 0.3f, 1.0f),
		Vector3(-0.85f, 0.3f, -1.0f), Vector3(0.85f, 0.3f, -1.0f)
	};
	static const char *wheel_names[4] = { "Roda_Front_Esq", "Roda_Front_Dir", "Roda_Tras_Esq", "Roda_Tras_Dir" };
	for (int i = 0; i < 4; i++) {
		MeshInstance3D *w = _add_prim(carro, wheel_names[i], roda, wheel_pos[i], Color(0.12f, 0.12f, 0.12f), 0.3f);
		w->set_rotation_degrees(Vector3(0, 0, 90));
	}
	return carro;
}

// Rampa solida: cunha visual + colisao inclinada.
static StaticBody3D *_recipe_ramp() {
	StaticBody3D *rampa = memnew(StaticBody3D);
	rampa->set_name("Rampa");
	Ref<PrismMesh> cunha;
	cunha.instantiate();
	cunha->set_size(Vector3(3.0f, 1.2f, 3.0f));
	cunha->set_left_to_right(1.0f);
	MeshInstance3D *vis = _add_prim(rampa, "Visual", cunha, Vector3(0, 0.6f, 0), Color(0.5f, 0.3f, 0.7f));
	vis->set_rotation_degrees(Vector3(0, 180, 0));
	Ref<BoxShape3D> sh;
	sh.instantiate();
	sh->set_size(Vector3(3.0f, 0.3f, 3.4f));
	CollisionShape3D *cs = memnew(CollisionShape3D);
	cs->set_name("Colisao");
	cs->set_shape(sh);
	cs->set_position(Vector3(0, 0.6f, 0));
	cs->set_rotation_degrees(Vector3(-19.5f, 0, 0));
	rampa->add_child(cs);
	return rampa;
}

// Cristal brilhante.
static Node3D *_recipe_crystal(const Color &p_cor) {
	Node3D *cristal = memnew(Node3D);
	cristal->set_name("Cristal");
	Ref<PrismMesh> gema;
	gema.instantiate();
	gema->set_size(Vector3(0.5f, 1.8f, 0.5f));
	gema->set_left_to_right(0.25f);
	MeshInstance3D *vis = _add_prim(cristal, "Gema", gema, Vector3(0, 0.9f, 0), p_cor, 0.2f, true);
	vis->set_rotation_degrees(Vector3(0, 45, 0));
	// base de pedra
	Ref<SphereMesh> base;
	base.instantiate();
	base->set_radius(0.5f);
	base->set_height(0.6f);
	_add_prim(cristal, "Base", base, Vector3(0, 0.12f, 0), Color(0.35f, 0.35f, 0.4f));
	return cristal;
}

// Moeda dourada girada (flutuando).
static Node3D *_recipe_coin() {
	Node3D *moeda = memnew(Node3D);
	moeda->set_name("Moeda");
	Ref<CylinderMesh> disco;
	disco.instantiate();
	disco->set_top_radius(0.4f);
	disco->set_bottom_radius(0.4f);
	disco->set_height(0.08f);
	MeshInstance3D *vis = _add_prim(moeda, "Disco", disco, Vector3(0, 1.2f, 0), Color(1.0f, 0.8f, 0.15f), 0.8f, true);
	vis->set_rotation_degrees(Vector3(90, 0, 0));
	return moeda;
}

// Plataforma flutuante.
static Node3D *_recipe_platform(const Color &p_cor) {
	StaticBody3D *plat = memnew(StaticBody3D);
	plat->set_name("Plataforma");
	Ref<CylinderMesh> disco;
	disco.instantiate();
	disco->set_top_radius(1.2f);
	disco->set_bottom_radius(1.3f);
	disco->set_height(0.3f);
	_add_prim(plat, "Visual", disco, Vector3(0, 0, 0), p_cor, 0.3f);
	Ref<CylinderShape3D> sh;
	sh.instantiate();
	sh->set_radius(1.25f);
	sh->set_height(0.3f);
	CollisionShape3D *cs = memnew(CollisionShape3D);
	cs->set_name("Colisao");
	cs->set_shape(sh);
	plat->add_child(cs);
	plat->set_position(Vector3(0, 1.6f, 0));
	return plat;
}

// Arma com corpo, cano, cabo e mira (metal de verdade).
static Node3D *_recipe_gun(const Color &p_cor) {
	Node3D *arma = memnew(Node3D);
	arma->set_name("Arma");
	// corpo
	Ref<BoxMesh> corpo;
	corpo.instantiate();
	corpo->set_size(Vector3(0.12f, 0.2f, 0.55f));
	_add_prim(arma, "Corpo", corpo, Vector3(0, 0, 0.05f), p_cor, 0.65f);
	// cano
	Ref<CylinderMesh> cano;
	cano.instantiate();
	cano->set_top_radius(0.035f);
	cano->set_bottom_radius(0.045f);
	cano->set_height(0.5f);
	MeshInstance3D *canom = _add_prim(arma, "Cano", cano, Vector3(0, 0.02f, 0.55f), Color(0.15f, 0.15f, 0.18f), 0.85f);
	canom->set_rotation_degrees(Vector3(90, 0, 0));
	// cabo
	Ref<BoxMesh> cabo;
	cabo.instantiate();
	cabo->set_size(Vector3(0.1f, 0.24f, 0.13f));
	MeshInstance3D *cabom = _add_prim(arma, "Cabo", cabo, Vector3(0, -0.18f, -0.18f), Color(0.2f, 0.14f, 0.08f), 0.1f);
	cabom->set_rotation_degrees(Vector3(-15, 0, 0));
	// mira
	Ref<BoxMesh> mira;
	mira.instantiate();
	mira->set_size(Vector3(0.04f, 0.05f, 0.12f));
	_add_prim(arma, "Mira", mira, Vector3(0, 0.14f, 0.05f), Color(0.1f, 0.1f, 0.12f), 0.7f);
	return arma;
}

// ---------------- plugin ---------------------------------------------------

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
	subtitle->set_text(TTR("Peça em português: \"cria um mapa bonito\", \"faz 3 árvores verdes\", \"cria um personagem\", \"faz um carro vermelho\"... Depois de autorizar, eu monto na sua tela ao vivo."));
	subtitle->add_theme_font_size_override("font_size", 13);
	subtitle->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
	panel->add_child(subtitle);

	// Botoes rapidos (nao precisa digitar nada pra comecar).
	quick_row = memnew(HBoxContainer);
	quick_row->add_theme_constant_override("separation", 6);
	quick_row->add_child(_make_quick_button(TTR("Mapa"), TTR("cria um mapa bonito pra pvp")));
	quick_row->add_child(_make_quick_button(TTR("Colisão"), TTR("adiciona colisão no cenário")));
	quick_row->add_child(_make_quick_button(TTR("Arma"), TTR("cria uma arma bonita")));
	panel->add_child(quick_row);

	// Segunda fileira de atalhos.
	quick_row2 = memnew(HBoxContainer);
	quick_row2->add_theme_constant_override("separation", 6);
	quick_row2->add_child(_make_quick_button(TTR("Personagem"), TTR("cria um personagem pra eu jogar")));
	quick_row2->add_child(_make_quick_button(TTR("Casa"), TTR("faz uma casa bonita")));
	quick_row2->add_child(_make_quick_button(TTR("HUD Mobile"), TTR("adiciona controles de celular: joystick e botões de pular, atirar e recarregar")));
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

	_append_chat("NEX", TTR("Oi! Eu sou a NEX.\n\nEscreva o que quiser em português: \"cria um mapa bonito\", \"faz 4 árvores\", \"cria um personagem azul\", \"faz um carro vermelho\", \"monta um lobby\", \"adiciona HUD mobile\"... Eu entendo cor, quantidade e tamanho (grande, pequeno). Depois que você autorizar, eu monto tudo AO VIVO na sua tela. Perguntas sobre o editor eu respondo na hora."), NEX_PURPLE_LIGHT);
}

void NexAIChatPlugin::_append_chat(const String &p_who, const String &p_text, const Color &p_color) {
	String color_hex = p_color.to_html(false);
	chat->append_text("[color=#" + color_hex + "][b]" + p_who + ":[/b][/color] " + p_text + "\n\n");
}

void NexAIChatPlugin::_show_tutorial() {
	_append_chat("NEX", TTR(
			"Como eu funciono:\n\n"
			"1) Peça em português normal: \"cria um mapa bonito pra pvp\", \"faz 3 árvores verdes\", \"cria um personagem\", \"faz um carro vermelho grande\", \"monta um lobby\", \"adiciona joystick\"...\n\n"
			"2) Eu entendo detalhes: cor (verde, vermelho, dourado...), quantidade (até 10) e tamanho (pequeno, grande, gigante).\n\n"
			"3) Toque em \"SIM, PODE FAZER\" e eu monto tudo AO VIVO na sua cena, com modelos compostos de verdade (árvore com tronco e copa, casa com telhado, carro com rodas), passo a passo.\n\n"
			"4) Perguntas sobre o editor (salvar, rodar, exportar APK) eu respondo direto.\n\n"
			"Lembrete: eu não copio jogos de outras empresas (Free Fire, Roblox etc), mas crio originais bonitos com a sua cara."),
			NEX_PURPLE_LIGHT);
}

void NexAIChatPlugin::_queue_live(int p_kind, const String &p_narration) {
	live_kinds.push_back(p_kind);
	live_narrations.push_back(p_narration);
	live_total++;
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
		answer = TTR("Boa pergunta! Nessa versão eu domino o básico do editor: criar cena (botão + e escolher 2D ou 3D), salvar (menu Projeto), rodar (triângulo), exportar APK (Projeto > Exportar) e criar coisas ao vivo (mapa bonito, colisão, arma, personagem, casa, carro, árvores, cristal, moedas, plataformas, HUD mobile, lobby). Perguntas mais avançadas chegam com meu cérebro completo na próxima versão.");
	}
	_append_chat("NEX", answer, NEX_PURPLE_LIGHT);
}

void NexAIChatPlugin::_classify_and_ask(const String &p_low) {
	// Extrai cor, quantidade e tamanho do pedido.
	pending_color_valid = _color_from_text(p_low, pending_color);
	pending_count = _count_from_text(p_low);
	pending_scale = _scale_from_text(p_low);


	if (p_low.contains("mapa") || p_low.contains("mundo") || p_low.contains("arena") || p_low.contains("terreno")) {
		pending_action = ACTION_MAP;
		_append_chat("NEX", TTR("Mapa PvP! Vou montar na sua frente, passo a passo: chão com colisão, 4 paredes, luz do sol, céu bonito, árvores, rampa, plataformas, moedas e pontos de spawn. Vai ficar bonito. Pode ser?"), NEX_PURPLE_LIGHT);
	} else if (p_low.contains("colis") || p_low.contains("bloqueio")) {
		pending_action = ACTION_COLLISION;
		_append_chat("NEX", TTR("Colisão! Vou adicionar uma caixa sólida na cena (nada atravessa ela). Pode ser?"), NEX_PURPLE_LIGHT);
	} else if (p_low.contains("arma") || p_low.contains("tiro") || p_low.contains("weapon") || p_low.contains("gun")) {
		pending_action = ACTION_WEAPON;
		_append_chat("NEX", TTR("Arma! Vou montar um modelo de verdade: corpo metálico, cano, cabo e mira. Pode ser?"), NEX_PURPLE_LIGHT);
	} else if (p_low.contains("arvore")) {
		pending_action = ACTION_TREE;
		_append_chat("NEX", vformat(TTR("Árvores! Vou plantar %d árvore(s) de verdade: tronco de madeira e copa esférica %s. Pode ser?"), pending_count, pending_color_valid ? TTR("na cor que você pediu") : TTR("verde")), NEX_PURPLE_LIGHT);
	} else if (p_low.contains("personagem") || p_low.contains("boneco") || p_low.contains("jogador") || p_low.contains("npc")) {
		pending_action = ACTION_CHAR;
		_append_chat("NEX", TTR("Personagem! Vou montar um boneco completo: corpo, cabeça, braços, pernas e colisão pra ele poder se mover com script depois. Pode ser?"), NEX_PURPLE_LIGHT);
	} else if (p_low.contains("casa") || p_low.contains("construcao") || p_low.contains("prédio") || p_low.contains("predio")) {
		pending_action = ACTION_HOUSE;
		_append_chat("NEX", TTR("Casa! Vou construir de verdade: chão, 4 paredes, porta, janelas iluminadas e telhado. Pode ser?"), NEX_PURPLE_LIGHT);
	} else if (p_low.contains("carro") || p_low.contains("veiculo")) {
		pending_action = ACTION_CAR;
		_append_chat("NEX", TTR("Carro! Vou montar com corpo metálico, cabine com vidro e 4 rodas. Pode ser?"), NEX_PURPLE_LIGHT);
	} else if (p_low.contains("rampa") || p_low.contains("solo inclinado")) {
		pending_action = ACTION_RAMP;
		_append_chat("NEX", TTR("Rampa! Vou fazer uma rampa sólida (dá pra subir nela de verdade). Pode ser?"), NEX_PURPLE_LIGHT);
	} else if (p_low.contains("cristal") || p_low.contains("gema") || p_low.contains("diamante")) {
		pending_action = ACTION_CRYSTAL;
		_append_chat("NEX", TTR("Cristal! Vou criar uma gema brilhante (da pra fazer sistema de coleta depois). Pode ser?"), NEX_PURPLE_LIGHT);
	} else if (p_low.contains("moeda") || p_low.contains("coin") || p_low.contains("dinheiro")) {
		pending_action = ACTION_COIN;
		_append_chat("NEX", vformat(TTR("Moedas! Vou espalhar %d moeda(s) dourada(s) brilhando pela cena. Pode ser?"), pending_count), NEX_PURPLE_LIGHT);
	} else if (p_low.contains("plataforma")) {
		pending_action = ACTION_PLATFORM;
		_append_chat("NEX", vformat(TTR("Plataformas! Vou criar %d plataforma(s) flutuante(s) sólida(s). Pode ser?"), pending_count), NEX_PURPLE_LIGHT);
	} else if (p_low.contains("joystick") || p_low.contains("hud") || p_low.contains("celular") || p_low.contains("mobile") || p_low.contains("controle")) {
		pending_action = ACTION_HUD;
		_append_chat("NEX", TTR("HUD Mobile! Vou montar o joystick na esquerda e os botões PULAR, ATIRAR e RECARREGAR na direita. Ele aparece na tela quando o jogo roda (play). Pode ser?"), NEX_PURPLE_LIGHT);
	} else if (p_low.contains("lobby") || p_low.contains("menu inicial") || p_low.contains("tela inicial") || p_low.contains("entrada")) {
		pending_action = ACTION_LOBBY;
		_append_chat("NEX", TTR("Lobby! Vou montar a tela de entrada: fundo, título e o botão JOGAR. Pode ser?"), NEX_PURPLE_LIGHT);
	} else {
		pending_action = ACTION_GENERIC;
		_append_chat("NEX", vformat(TTR("Entendi! Vou criar \"%s\" como um objeto 3D %s na sua cena, ao vivo. Pode ser?"), pending_text, pending_color_valid ? TTR("na cor que você pediu") : TTR("")), NEX_PURPLE_LIGHT);
	}
	perm_row->show();
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
	pending_count = 1;
	pending_scale = 1.0f;
	pending_color_valid = false;

	String low = _unaccent(txt.to_lower());

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
	if (low == "oi" || low == "ola" || low == "eai" || low == "e ai" ||
			_greet_word(low) || low.contains("bom dia") || low.contains("boa tarde") || low.contains("boa noite") || low.contains("tudo bem")) {
		_append_chat("NEX", TTR("Opa! Tô on. Me pede pra criar: mapa bonito, personagem, casa, carro, árvores, arma, HUD mobile, lobby... Ou pergunta qualquer coisa sobre o editor que eu explico."), NEX_PURPLE_LIGHT);
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
	Node *root = EditorNode::get_singleton()->get_edited_scene();
	if (root == nullptr) {
		_append_chat("NEX", TTR("Ei, ainda não tem cena aberta (ou tá vazia). Toque no + pra criar uma \"Cena 3D\" e me pede de novo que eu faço na hora."), NEX_PURPLE_LIGHT);
		pending_action = ACTION_NONE;
		return;
	}

	// Acoes 3D precisam de raiz Node3D.
	bool needs_3d = (pending_action == ACTION_MAP || pending_action == ACTION_COLLISION ||
			pending_action == ACTION_WEAPON || pending_action == ACTION_TREE ||
			pending_action == ACTION_CHAR || pending_action == ACTION_HOUSE ||
			pending_action == ACTION_CAR || pending_action == ACTION_RAMP ||
			pending_action == ACTION_CRYSTAL || pending_action == ACTION_COIN ||
			pending_action == ACTION_PLATFORM || pending_action == ACTION_GENERIC);
	if (needs_3d && Object::cast_to<Node3D>(root) == nullptr) {
		_append_chat("NEX", TTR("Sua cena atual não é 3D. Toque no + pra criar uma \"Cena 3D\" (a que tem profundidade) e me pede de novo. HUD e Lobby eu consigo criar aqui mesmo."), NEX_PURPLE_LIGHT);
		pending_action = ACTION_NONE;
		return;
	}

	live_narrations.clear();
	live_kinds.clear();
	live_index = 0;
	live_total = 0;

	_append_chat("NEX", TTR("Fechado! Modo ao vivo ligado. Olha a cena 3D que eu vou montando..."), NEX_PURPLE_LIGHT);

	switch (pending_action) {
		case ACTION_MAP:
			_queue_live(STEP_GROUND, TTR("Criando o chão do mapa (20x20) com colisão..."));
			_queue_live(STEP_WALLS, TTR("Levantando as 4 paredes da arena..."));
			_queue_live(STEP_LIGHT_ENV, TTR("Acendendo o sol e pintando o céu..."));
			_queue_live(STEP_DECOR, TTR("Decorando: árvores, rampa, plataformas, moedas e spawns..."));
			break;
		case ACTION_COLLISION:
			_queue_live(STEP_COLLISION, TTR("Adicionando a caixa de colisão sólida..."));
			break;
		case ACTION_WEAPON:
			_queue_live(STEP_GUN, TTR("Forjando a arma: corpo, cano, cabo e mira..."));
			break;
		case ACTION_TREE:
			_queue_live(STEP_TREE, TTR("Plantando as árvores..."));
			break;
		case ACTION_CHAR:
			_queue_live(STEP_CHAR, TTR("Montando o personagem: corpo, cabeça, braços e pernas..."));
			break;
		case ACTION_HOUSE:
			_queue_live(STEP_HOUSE, TTR("Construindo a casa: paredes, porta, janelas e telhado..."));
			break;
		case ACTION_CAR:
			_queue_live(STEP_CAR, TTR("Montando o carro: lataria, vidros e rodas..."));
			break;
		case ACTION_RAMP:
			_queue_live(STEP_RAMP, TTR("Erguendo a rampa sólida..."));
			break;
		case ACTION_CRYSTAL:
			_queue_live(STEP_CRYSTAL, TTR("Cortando o cristal brilhante..."));
			break;
		case ACTION_COIN:
			_queue_live(STEP_COIN, TTR("Espalhando as moedas douradas..."));
			break;
		case ACTION_PLATFORM:
			_queue_live(STEP_PLATFORM, TTR("Posicionando as plataformas flutuantes..."));
			break;
		case ACTION_HUD:
			_queue_live(STEP_HUD, TTR("Montando o HUD mobile: joystick + botões PULAR, ATIRAR e RECARREGAR..."));
			break;
		case ACTION_LOBBY:
			_queue_live(STEP_LOBBY, TTR("Montando o lobby: fundo, título e botão JOGAR..."));
			break;
		default:
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
				_append_chat("NEX", TTR("PRONTINHO! Mapa PvP montado: chão, 4 paredes, sol, céu, árvores, rampa, plataformas, moedas e spawns. Vê a aba da cena 3D (perspectiva). Salva no menu Projeto pra não perder!"), NEX_PURPLE_LIGHT);
				break;
			case ACTION_HUD:
				_append_chat("NEX", TTR("PRONTINHO! HUD mobile montado (joystick + PULAR, ATIRAR, RECARREGAR). Ele aparece quando você RODA o jogo (triângulo), porque controle de toque é do jogo, não do editor."), NEX_PURPLE_LIGHT);
				break;
			case ACTION_LOBBY:
				_append_chat("NEX", TTR("PRONTINHO! Lobby montado: fundo, título e botão JOGAR. Roda o jogo (play) pra ver ele na tela."), NEX_PURPLE_LIGHT);
				break;
			case ACTION_TREE:
				_append_chat("NEX", TTR("PRONTINHO! Árvores plantadas com tronco e copa de verdade."), NEX_PURPLE_LIGHT);
				break;
			case ACTION_CHAR:
				_append_chat("NEX", TTR("PRONTINHO! Personagem montado com colisão: dá pra fazer ele andar com um script de movimento depois."), NEX_PURPLE_LIGHT);
				break;
			default:
				_append_chat("NEX", TTR("PRONTINHO! Tá na sua cena. Salva no menu Projeto pra não perder!"), NEX_PURPLE_LIGHT);
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
	if (root != nullptr && root->has_node(NodePath("Mapa_PvP"))) {
		mapa = root->get_node(NodePath("Mapa_PvP"));
	}
	Color cor = pending_color_valid ? pending_color : NEX_PURPLE;

	switch (kind) {
		case STEP_GROUND: {
			Node3D *map = memnew(Node3D);
			map->set_name("Mapa_PvP");
			// Chao solido com mesh + colisao.
			StaticBody3D *chao = memnew(StaticBody3D);
			chao->set_name("Chao");
			chao->set_position(Vector3(0, -0.25f, 0));
			Ref<BoxMesh> chao_box;
			chao_box.instantiate();
			chao_box->set_size(Vector3(20, 0.5f, 20));
			_add_prim(chao, "Chao_Visual", chao_box, Vector3(0, 0, 0), Color(0.18f, 0.12f, 0.28f));
			Ref<BoxShape3D> chao_shape;
			chao_shape.instantiate();
			chao_shape->set_size(Vector3(20, 0.5f, 20));
			CollisionShape3D *chao_cs = memnew(CollisionShape3D);
			chao_cs->set_name("Chao_Colisao");
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
				Ref<BoxMesh> wb;
				wb.instantiate();
				wb->set_size(sizes[i]);
				_add_prim(wall, "Visual", wb, Vector3(0, 0, 0), Color(0.45f, 0.28f, 0.65f));
				Ref<BoxShape3D> wsh;
				wsh.instantiate();
				wsh->set_size(sizes[i]);
				CollisionShape3D *wcs = memnew(CollisionShape3D);
				wcs->set_name("Colisao");
				wcs->set_shape(wsh);
				wall->add_child(wcs);
				paredes->add_child(wall);
			}
			_add_node_live(parent, paredes, TTR("NEX: criar paredes do mapa"));
			break;
		}
		case STEP_LIGHT_ENV: {
			Node *parent = mapa != nullptr ? mapa : root;
			DirectionalLight3D *sol = memnew(DirectionalLight3D);
			sol->set_name("Luz_Sol");
			sol->set_rotation_degrees(Vector3(-45, -30, 0));
			_add_node_live(parent, sol, TTR("NEX: acender luz do sol"));
			// Ceu procedural (roxo-azul do por do sol da NEXUS).
			Ref<ProceduralSkyMaterial> psm;
			psm.instantiate();
			psm->set_sky_top_color(Color(0.25f, 0.12f, 0.45f));
			psm->set_sky_horizon_color(Color(0.75f, 0.4f, 0.65f));
			psm->set_ground_bottom_color(Color(0.15f, 0.08f, 0.2f));
			psm->set_ground_horizon_color(Color(0.55f, 0.3f, 0.5f));
			Ref<Sky> sky;
			sky.instantiate();
			sky->set_material(psm);
			Ref<Environment> env;
			env.instantiate();
			env->set_background(Environment::BG_SKY);
			env->set_sky(sky);
			env->set_ambient_source(Environment::AMBIENT_SOURCE_SKY);
			env->set_ambient_light_energy(1.2f);
			WorldEnvironment *we = memnew(WorldEnvironment);
			we->set_name("Ceu_NEXUS");
			we->set_environment(env);
			_add_node_live(parent, we, TTR("NEX: pintar o ceu"));
			break;
		}
		case STEP_DECOR: {
			Node *parent = mapa != nullptr ? mapa : root;
			Node3D *decor = memnew(Node3D);
			decor->set_name("Decoracao");
			// 4 arvores nos cantos.
			static const Vector3 tree_pos[4] = {
				Vector3(-6, 0, -6), Vector3(6, 0, -6), Vector3(-6, 0, 6), Vector3(6, 0, 6)
			};
			for (int i = 0; i < 4; i++) {
				Node3D *t = _recipe_tree(Color(0.2f, 0.7f, 0.25f));
				t->set_name("Arvore_" + String::num_int64(i + 1));
				t->set_position(tree_pos[i]);
				decor->add_child(t);
			}
			// Rampa.
			StaticBody3D *r = _recipe_ramp();
			r->set_position(Vector3(5, 0, 0));
			decor->add_child(r);
			// 2 plataformas flutuantes.
			Node3D *p1 = _recipe_platform(Color(0.5f, 0.3f, 0.75f));
			p1->set_name("Plataforma_1");
			p1->set_position(Vector3(-4, 1.6f, -4));
			decor->add_child(p1);
			Node3D *p2 = _recipe_platform(Color(0.5f, 0.3f, 0.75f));
			p2->set_name("Plataforma_2");
			p2->set_position(Vector3(4, 2.6f, 4));
			decor->add_child(p2);
			// 4 moedas.
			static const Vector3 coin_pos[4] = {
				Vector3(-3, 0, 3), Vector3(3, 0, -3), Vector3(0, 0, 5), Vector3(0, 0, -5)
			};
			for (int i = 0; i < 4; i++) {
				Node3D *c = _recipe_coin();
				c->set_name("Moeda_" + String::num_int64(i + 1));
				c->set_position(coin_pos[i]);
				decor->add_child(c);
			}
			// 2 pontos de spawn (cilindros verdes brilhando).
			for (int i = 0; i < 2; i++) {
				Ref<CylinderMesh> sp;
				sp.instantiate();
				sp->set_top_radius(0.5f);
				sp->set_bottom_radius(0.5f);
				sp->set_height(0.05f);
				_add_prim(decor, "Spawn_" + String::num_int64(i + 1), sp,
						Vector3(0, 0.28f, i == 0 ? 7.0f : -7.0f), Color(0.1f, 0.9f, 0.3f), 0.0f, true);
			}
			_add_node_live(parent, decor, TTR("NEX: decorar o mapa"));
			break;
		}
		case STEP_COLLISION: {
			StaticBody3D *col = memnew(StaticBody3D);
			col->set_name("Colisao");
			col->set_position(Vector3(0, 1, 0));
			Ref<BoxShape3D> sh;
			sh.instantiate();
			sh->set_size(Vector3(2, 2, 2));
			CollisionShape3D *cs = memnew(CollisionShape3D);
			cs->set_name("Formato");
			cs->set_shape(sh);
			col->add_child(cs);
			_add_node_live(root, col, TTR("NEX: adicionar colisao"));
			break;
		}
		case STEP_GUN: {
			Node3D *arma = _recipe_gun(pending_color_valid ? cor : Color(0.2f, 0.12f, 0.32f));
			arma->set_position(Vector3(1, 1, 0));
			arma->set_scale(Vector3(pending_scale, pending_scale, pending_scale));
			_add_node_live(root, arma, TTR("NEX: criar arma"));
			break;
		}
		case STEP_HUD: {
			CanvasLayer *hud = memnew(CanvasLayer);
			hud->set_name("HUD_Mobile");
			ColorRect *base = memnew(ColorRect);
			base->set_name("Base_Joystick");
			base->set_color(Color(0.24f, 0.12f, 0.36f, 0.55f));
			base->set_anchors_and_offsets_preset(Control::PRESET_BOTTOM_LEFT, Control::PRESET_MODE_MINSIZE);
			base->set_size(Size2(120, 120));
			base->set_position(Point2(30, -170));
			hud->add_child(base);
			ColorRect *bola = memnew(ColorRect);
			bola->set_name("Bola_Joystick");
			bola->set_color(Color(0.66f, 0.33f, 0.97f, 0.9f));
			bola->set_anchors_and_offsets_preset(Control::PRESET_BOTTOM_LEFT, Control::PRESET_MODE_MINSIZE);
			bola->set_size(Size2(50, 50));
			bola->set_position(Point2(65, -135));
			hud->add_child(bola);
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
		case STEP_TREE: {
			Node3D *grupo = memnew(Node3D);
			grupo->set_name("Arvores");
			Color copa = pending_color_valid ? cor : Color(0.2f, 0.7f, 0.25f);
			for (int i = 0; i < pending_count; i++) {
				Node3D *t = _recipe_tree(copa);
				t->set_name("Arvore_" + String::num_int64(i + 1));
				t->set_position(Vector3((i - pending_count / 2.0f) * 2.5f, 0, 0));
				t->set_scale(Vector3(pending_scale, pending_scale, pending_scale));
				grupo->add_child(t);
			}
			_add_node_live(root, grupo, TTR("NEX: plantar arvores"));
			break;
		}
		case STEP_CHAR: {
			CharacterBody3D *chr = _recipe_char(pending_color_valid ? cor : Color(0.4f, 0.25f, 0.85f));
			chr->set_scale(Vector3(pending_scale, pending_scale, pending_scale));
			_add_node_live(root, chr, TTR("NEX: criar personagem"));
			break;
		}
		case STEP_HOUSE: {
			Node3D *casa = _recipe_house(pending_color_valid ? cor : Color(0.85f, 0.8f, 0.7f));
			casa->set_scale(Vector3(pending_scale, pending_scale, pending_scale));
			_add_node_live(root, casa, TTR("NEX: construir casa"));
			break;
		}
		case STEP_CAR: {
			Node3D *carro = _recipe_car(pending_color_valid ? cor : Color(0.8f, 0.15f, 0.15f));
			carro->set_scale(Vector3(pending_scale, pending_scale, pending_scale));
			_add_node_live(root, carro, TTR("NEX: montar carro"));
			break;
		}
		case STEP_RAMP: {
			StaticBody3D *r = _recipe_ramp();
			r->set_scale(Vector3(pending_scale, pending_scale, pending_scale));
			_add_node_live(root, r, TTR("NEX: erguer rampa"));
			break;
		}
		case STEP_CRYSTAL: {
			Node3D *cristal = _recipe_crystal(pending_color_valid ? cor : Color(0.5f, 0.3f, 0.95f));
			cristal->set_scale(Vector3(pending_scale, pending_scale, pending_scale));
			_add_node_live(root, cristal, TTR("NEX: cortar cristal"));
			break;
		}
		case STEP_COIN: {
			Node3D *grupo = memnew(Node3D);
			grupo->set_name("Moedas");
			for (int i = 0; i < pending_count; i++) {
				Node3D *c = _recipe_coin();
				c->set_name("Moeda_" + String::num_int64(i + 1));
				c->set_position(Vector3((i - pending_count / 2.0f) * 1.5f, 0, (i % 2 == 0) ? 1.0f : -1.0f));
				c->set_scale(Vector3(pending_scale, pending_scale, pending_scale));
				grupo->add_child(c);
			}
			_add_node_live(root, grupo, TTR("NEX: espalhar moedas"));
			break;
		}
		case STEP_PLATFORM: {
			Node3D *grupo = memnew(Node3D);
			grupo->set_name("Plataformas");
			for (int i = 0; i < pending_count; i++) {
				Node3D *p = _recipe_platform(pending_color_valid ? cor : Color(0.5f, 0.3f, 0.75f));
				p->set_name("Plataforma_" + String::num_int64(i + 1));
				// escadinha flutuante
				p->set_position(Vector3((i - pending_count / 2.0f) * 2.0f, 1.0f + i * 0.8f, 0));
				p->set_scale(Vector3(pending_scale, pending_scale, pending_scale));
				grupo->add_child(p);
			}
			_add_node_live(root, grupo, TTR("NEX: criar plataformas"));
			break;
		}
		default: { // STEP_GENERIC
			MeshInstance3D *obj = memnew(MeshInstance3D);
			obj->set_name("Objeto");
			Ref<BoxMesh> gb;
			gb.instantiate();
			gb->set_size(Vector3(1, 1, 1));
			obj->set_mesh(gb);
			obj->set_material_override(_mat(cor, 0.2f));
			obj->set_scale(Vector3(pending_scale, pending_scale, pending_scale));
			_add_node_live(root, obj, TTR("NEX: criar objeto"));
			break;
		}
	}

	live_index++;
	// Pausa entre passos pra dar o efeito "fazendo ao vivo".
	get_tree()->create_timer(0.9f)->connect("timeout", callable_mp(this, &NexAIChatPlugin::_live_next_step));
}

bool NexAIChatPlugin::_add_node_live(Node *p_parent, Node *p_child, const String &p_action_name) {
	Node *edited_scene = EditorNode::get_singleton()->get_edited_scene();
	if (p_parent == nullptr || p_child == nullptr) {
		return false;
	}
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
