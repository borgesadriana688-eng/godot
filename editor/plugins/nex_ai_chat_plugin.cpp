/**************************************************************************/
/*  nex_ai_chat_plugin.cpp                                                */
/*  NEX AI — chat dock lateral da Nexus Engine                             */
/*  Spec: NEX_AI.md (documento oficial do módulo NEX)                      */
/*  v0.3 — MODO AO VIVO + RECEITAS VISUAIS COMPOSTAS:                      */
/*  modelos de verdade (nao tudo quadrado), cores, tamanhos e               */
/*  quantidades entendidos direto do pedido do dev.                        */
/**************************************************************************/

#include "editor/plugins/nex_ai_chat_plugin.h"

#include "core/config/project_settings.h"
#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/io/http_client.h"
#include "core/io/json.h"
#include "core/math/color.h"
#include "core/object/callable_mp.h"
#include "core/variant/dictionary.h"
#include "core/string/node_path.h"
#include "core/string/ustring.h"
#include "core/variant/variant.h"
#include "editor/editor_data.h"
#include "editor/editor_node.h"
#include "editor/editor_interface.h"
#include "scene/3d/camera_3d.h"
#include "scene/3d/label_3d.h"
#include "scene/main/viewport.h"
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
#include "scene/main/http_request.h"
#include "scene/main/scene_tree.h"
#include "scene/resources/3d/box_shape_3d.h"
#include "scene/resources/3d/capsule_shape_3d.h"
#include "scene/resources/3d/cylinder_shape_3d.h"
#include "scene/resources/3d/primitive_meshes.h"
#include "scene/resources/3d/sky_material.h"
#include "scene/resources/environment.h"
#include "scene/resources/sky.h"
#include "scene/resources/material.h"
#include "modules/gltf/gltf_document.h"
#include "modules/gltf/gltf_state.h"

// OBS: nao usar emoji (caracteres acima de U+FFFF) em nenhum texto desta
// tela. A fonte do editor Android nao tem esses glifos e eles aparecem
// quebrados. Letras acentuadas normais (ã, ç, é) funcionam bem.

static const char *NEX_ASSETS_URL = "https://github.com/borgesadriana688-eng/godot/releases/download/nex-assets-v1/";
static const char *NEX_BRAIN_URL = "https://elio-43de708f.base44.app/functions/kryno";

static const Color NEX_PURPLE(0.659f, 0.333f, 0.969f, 1.0f);
static const Color NEX_PURPLE_LIGHT(0.769f, 0.518f, 0.988f, 1.0f);

// Passos do modo ao vivo.
enum NexLiveStep {
	STEP_FETCH,
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
	STEP_BR_TERRAIN,
	STEP_BR_TOWN,
	STEP_BR_CITY,
	STEP_BR_GAS,
	STEP_BR_FOREST,
	STEP_BR_ROADS,
	STEP_BR_LOOT,
	STEP_BR_ZONE,
	STEP_WALL,
	STEP_CHEST,
	STEP_EFFECT,
	STEP_RECOLOR,
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


// Escolhe o modelo real (.glb) pro pedido; nullptr se nao tem modelo real.
static const char *_asset_for(NexAIChatPlugin::PendingAction p_action, const String &p_low) {
	switch (p_action) {
		case NexAIChatPlugin::ACTION_TREE: return "nex_arvore.glb";
		case NexAIChatPlugin::ACTION_CHAR: return "nex_personagem.glb";
		case NexAIChatPlugin::ACTION_HOUSE: return p_low.contains("garagem") ? "nex_garagem.glb" : "nex_casa.glb";
		case NexAIChatPlugin::ACTION_CAR:
			if (p_low.contains("moto")) {
				return "nex_moto.glb";
			}
			if (p_low.contains("vermelho")) {
				return "nex_carro_vermelho.glb";
			}
			if (p_low.contains("verde")) {
				return "nex_carro_verde.glb";
			}
			return "nex_carro_roxo.glb";
		case NexAIChatPlugin::ACTION_WEAPON: return p_low.contains("rifle") ? "nex_arma_rifle.glb" : "nex_arma.glb";
		case NexAIChatPlugin::ACTION_COIN: return "nex_moeda.glb";
		case NexAIChatPlugin::ACTION_PLATFORM: return "nex_plataforma.glb";
		default: return nullptr;
	}
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


// Palavra de objeto (do cerebro ou do texto) -> modelo real. Catalogo CC0.
static const char *_asset_by_object_word(const String &p_word) {
	if (p_word.is_empty()) {
		return nullptr;
	}
	String w = _unaccent(p_word.to_lower());
	if (w->contains("arvore") || w->contains("floresta") || w->contains("planta")) {
		return "nex_arvore.glb";
	}
	if (w->contains("personagem") || w->contains("boneco") || w->contains("jogador") || w->contains("npc")) {
		return "nex_personagem.glb";
	}
	if (w->contains("garagem")) {
		return "nex_garagem.glb";
	}
	if (w->contains("casa") || w->contains("cabana") || w->contains("vila")) {
		return "nex_casa.glb";
	}
	if (w->contains("predio") || w->contains("hotel") || w->contains("torre") || w->contains("construc")) {
		return "nex_predio.glb";
	}
	if (w->contains("moto")) {
		return "nex_moto.glb";
	}
	if (w->contains("carro") || w->contains("caminhonete") || w->contains("veiculo")) {
		return "nex_carro_roxo.glb";
	}
	if (w->contains("rifle")) {
		return "nex_arma_rifle.glb";
	}
	if (w->contains("arma") || w->contains("pistola") || w->contains("blaster") || w->contains("tiro")) {
		return "nex_arma.glb";
	}
	if (w->contains("moeda") || w->contains("coin") || w->contains("dinheiro")) {
		return "nex_moeda.glb";
	}
	if (w->contains("plataforma") || w->contains("chao flutuante")) {
		return "nex_plataforma.glb";
	}
	if (w->contains("parede") || w->contains("muro") || w->contains("cerca")) {
		return "nex_parede.glb";
	}
	if (w->contains("estrada") || w->contains("rua") || w->contains("pista")) {
		return "nex_estrada.glb";
	}
	if (w->contains("fonte")) {
		return "nex_fonte.glb";
	}
	if (w->contains("grama") || w->contains("relva")) {
		return "nex_grama.glb";
	}
	if (w->contains("nuvem")) {
		return "nex_nuvem.glb";
	}
	if (w->contains("tenda") || w->contains("barraca")) {
		return "nex_tenda.glb";
	}
	if (w->contains("inimigo") || w->contains("monstro") || w->contains("boss")) {
		return "nex_inimigo.glb";
	}
	if (w->contains("bandeira") || w->contains("flag")) {
		return "nex_bandeira.glb";
	}
	return nullptr;
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
// Casa: chao + 4 paredes + porta + 2 janelas + telhado->
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



// ---------------- v8: receitas CARTOON LIMPAS ---------------------------

// Tipo de arma pelo texto: 0=AR, 1=shotgun, 2=sniper, 3=picareta.
static int _weapon_type_from_text(const String &p_low) {
	if (p_low.contains("picareta") || p_low.contains("machado")) {
		return 3;
	}
	if (p_low.contains("sniper") || p_low.contains("precisao")) {
		return 2;
	}
	if (p_low.contains("shotgun") || p_low.contains("escopeta")) {
		return 1;
	}
	return 0; // AR / fuzil padrao
}

// Material da parede pelo texto: 0=madeira, 1=tijolo, 2=metal.
static int _wall_mat_from_text(const String &p_low) {
	if (p_low.contains("metal") || p_low.contains("aco")) {
		return 2;
	}
	if (p_low.contains("tijolo") || p_low.contains("pedra")) {
		return 1;
	}
	return 0;
}

// Personagem CHIBI: cabeca grande, corpo pequeno, estilo cartoon.
static CharacterBody3D *_recipe_chibi(const Color &p_roupa, const Color &p_cabelo) {
	CharacterBody3D *chr = memnew(CharacterBody3D);
	chr->set_name("Personagem_Chibi");
	// corpo pequeno
	Ref<CapsuleMesh> corpo;
	corpo.instantiate();
	corpo->set_radius(0.26f);
	corpo->set_height(0.6f);
	_add_prim(chr, "Corpo", corpo, Vector3(0, 0.42f, 0), p_roupa);
	// cabeca GRANDE (chibi!)
	Ref<SphereMesh> cabeca;
	cabeca.instantiate();
	cabeca->set_radius(0.42f);
	cabeca->set_height(0.84f);
	_add_prim(chr, "Cabeca", cabeca, Vector3(0, 1.05f, 0), Color(0.96f, 0.78f, 0.62f));
	// cabelo (meia esfera achatada em cima)
	Ref<SphereMesh> cbl;
	cbl.instantiate();
	cbl->set_radius(0.44f);
	cbl->set_height(0.5f);
	_add_prim(chr, "Cabelo", cbl, Vector3(0, 1.16f, -0.03f), p_cabelo);
	// olhinhos cartoon
	Ref<SphereMesh> olho;
	olho.instantiate();
	olho->set_radius(0.05f);
	olho->set_height(0.1f);
	_add_prim(chr, "Olho_Esq", olho, Vector3(-0.13f, 1.08f, 0.38f), Color(0.1f, 0.1f, 0.12f));
	_add_prim(chr, "Olho_Dir", olho, Vector3(0.13f, 1.08f, 0.38f), Color(0.1f, 0.1f, 0.12f));
	// bracos curtos
	Ref<CapsuleMesh> braco;
	braco.instantiate();
	braco->set_radius(0.08f);
	braco->set_height(0.36f);
	MeshInstance3D *be = _add_prim(chr, "Braco_Esq", braco, Vector3(-0.33f, 0.5f, 0), p_roupa);
	be->set_rotation_degrees(Vector3(0, 0, 12));
	MeshInstance3D *bd = _add_prim(chr, "Braco_Dir", braco, Vector3(0.33f, 0.5f, 0), p_roupa);
	bd->set_rotation_degrees(Vector3(0, 0, -12));
	// pernas curtas
	Ref<CapsuleMesh> perna;
	perna.instantiate();
	perna->set_radius(0.09f);
	perna->set_height(0.28f);
	_add_prim(chr, "Perna_Esq", perna, Vector3(-0.12f, 0.12f, 0), Color(0.2f, 0.2f, 0.28f));
	_add_prim(chr, "Perna_Dir", perna, Vector3(0.12f, 0.12f, 0), Color(0.2f, 0.2f, 0.28f));
	// colisao
	Ref<CapsuleShape3D> sh;
	sh.instantiate();
	sh->set_radius(0.45f);
	sh->set_height(1.6f);
	CollisionShape3D *cs = memnew(CollisionShape3D);
	cs->set_name("Colisao");
	cs->set_shape(sh);
	cs->set_position(Vector3(0, 0.8f, 0));
	chr->add_child(cs);
	return chr;
}

// Parede de construcao: madeira / tijolo / metal (cores fortes, limpas).
static StaticBody3D *_recipe_wall(int p_mat) {
	StaticBody3D *wall = memnew(StaticBody3D);
	static const char *nomes[3] = { "Parede_Madeira", "Parede_Tijolo", "Parede_Metal" };
	wall->set_name(nomes[p_mat]);
	Color cor;
	float metal = 0.0f;
	switch (p_mat) {
		case 1: cor = Color(0.86f, 0.36f, 0.2f); break;  // tijolo
		case 2: cor = Color(0.62f, 0.68f, 0.78f); metal = 0.9f; break; // metal
		default: cor = Color(0.68f, 0.45f, 0.2f); break; // madeira
	}
	Ref<BoxMesh> vis;
	vis.instantiate();
	vis->set_size(Vector3(4, 3, 0.3f));
	_add_prim(wall, "Visual", vis, Vector3(0, 1.5f, 0), cor, metal);
	// listras cartoon de leveza
	Ref<BoxMesh> lista;
	lista.instantiate();
	lista->set_size(Vector3(4.02f, 0.18f, 0.34f));
	Color cor2 = cor.darkened(0.25f);
	_add_prim(wall, "Listra_1", lista, Vector3(0, 0.9f, 0), cor2, metal);
	_add_prim(wall, "Listra_2", lista, Vector3(0, 2.1f, 0), cor2, metal);
	Ref<BoxShape3D> sh;
	sh.instantiate();
	sh->set_size(Vector3(4, 3, 0.3f));
	CollisionShape3D *cs = memnew(CollisionShape3D);
	cs->set_name("Colisao");
	cs->set_shape(sh);
	cs->set_position(Vector3(0, 1.5f, 0));
	wall->add_child(cs);
	return wall;
}

// Bau de loot dourado brilhando (estilo cartoon).
static Node3D *_recipe_chest() {
	Node3D *bau = memnew(Node3D);
	bau->set_name("Bau_Loot");
	Color ouro(1.0f, 0.8f, 0.15f);
	// base
	Ref<BoxMesh> base;
	base.instantiate();
	base->set_size(Vector3(1.2f, 0.6f, 0.8f));
	_add_prim(bau, "Base", base, Vector3(0, 0.3f, 0), ouro, 0.55f);
	// tampa aberta (girada)
	Ref<BoxMesh> tampa;
	tampa.instantiate();
	tampa->set_size(Vector3(1.2f, 0.28f, 0.8f));
	MeshInstance3D *tm = _add_prim(bau, "Tampa", tampa, Vector3(0, 0.74f, -0.3f), ouro.darkened(0.1f), 0.55f);
	tm->set_rotation_degrees(Vector3(-35, 0, 0));
	// brilho lendario dentro
	Ref<SphereMesh> brilho;
	brilho.instantiate();
	brilho->set_radius(0.16f);
	brilho->set_height(0.32f);
	_add_prim(bau, "Brilho", brilho, Vector3(0, 0.55f, 0), Color(1, 0.95f, 0.5f), 0.0f, true);
	// pes
	Ref<BoxMesh> pe;
	pe.instantiate();
	pe->set_size(Vector3(0.15f, 0.1f, 0.8f));
	_add_prim(bau, "Pe_Esq", pe, Vector3(-0.5f, 0.05f, 0), Color(0.35f, 0.2f, 0.05f));
	_add_prim(bau, "Pe_Dir", pe, Vector3(0.5f, 0.05f, 0), Color(0.35f, 0.2f, 0.05f));
	return bau;
}

// Armas BONITAS por tipo, cor forte e brilho lendario.
static Node3D *_recipe_weapon_typed(int p_type, bool p_lendaria) {
	Node3D *arma = memnew(Node3D);
	static const char *nomes[4] = { "AR_Dourada", "Shotgun", "Sniper", "Picareta" };
	arma->set_name(nomes[p_type]);
	bool brilho = p_lendaria;
	switch (p_type) {
		case 1: { // SHOTGUN
			Ref<BoxMesh> corpo;
			corpo.instantiate();
			corpo->set_size(Vector3(0.16f, 0.22f, 0.5f));
			_add_prim(arma, "Corpo", corpo, Vector3(0, 0, 0), Color(0.85f, 0.4f, 0.08f), 0.4f);
			Ref<CylinderMesh> cano;
			cano.instantiate();
			cano->set_top_radius(0.05f);
			cano->set_bottom_radius(0.055f);
			cano->set_height(0.45f);
			MeshInstance3D *c1 = _add_prim(arma, "Cano_Dir", cano, Vector3(0.05f, 0.05f, 0.42f), Color(0.2f, 0.2f, 0.24f), 0.85f);
			c1->set_rotation_degrees(Vector3(90, 0, 0));
			MeshInstance3D *c2 = _add_prim(arma, "Cano_Esq", cano, Vector3(-0.05f, 0.05f, 0.42f), Color(0.2f, 0.2f, 0.24f), 0.85f);
			c2->set_rotation_degrees(Vector3(90, 0, 0));
			Ref<BoxMesh> cabo;
			cabo.instantiate();
			cabo->set_size(Vector3(0.12f, 0.3f, 0.15f));
			_add_prim(arma, "Cabo_Madeira", cabo, Vector3(0, -0.2f, -0.2f), Color(0.5f, 0.3f, 0.1f));
			if (brilho) {
				Ref<SphereMesh> gl;
				gl.instantiate();
				gl->set_radius(0.12f);
				gl->set_height(0.24f);
				_add_prim(arma, "Brilho_Lendario", gl, Vector3(0, 0.18f, 0), Color(1, 0.9f, 0.3f), 0.0f, true);
			}
			break;
		}
		case 2: { // SNIPER
			Ref<BoxMesh> corpo;
			corpo.instantiate();
			corpo->set_size(Vector3(0.1f, 0.16f, 0.6f));
			_add_prim(arma, "Corpo", corpo, Vector3(0, 0, 0), Color(0.15f, 0.42f, 0.95f), 0.4f);
			Ref<CylinderMesh> cano;
			cano.instantiate();
			cano->set_top_radius(0.03f);
			cano->set_bottom_radius(0.04f);
			cano->set_height(0.8f);
			MeshInstance3D *cm = _add_prim(arma, "Cano_Longo", cano, Vector3(0, 0.02f, 0.68f), Color(0.18f, 0.2f, 0.26f), 0.85f);
			cm->set_rotation_degrees(Vector3(90, 0, 0));
			Ref<CylinderMesh> luneta;
			luneta.instantiate();
			luneta->set_top_radius(0.05f);
			luneta->set_bottom_radius(0.05f);
			luneta->set_height(0.22f);
			MeshInstance3D *lm = _add_prim(arma, "Luneta", luneta, Vector3(0, 0.14f, 0.05f), Color(0.1f, 0.12f, 0.15f), 0.6f);
			lm->set_rotation_degrees(Vector3(90, 0, 0));
			Ref<BoxMesh> cabo;
			cabo.instantiate();
			cabo->set_size(Vector3(0.09f, 0.26f, 0.13f));
			_add_prim(arma, "Cabo", cabo, Vector3(0, -0.16f, -0.24f), Color(0.12f, 0.12f, 0.16f));
			if (brilho) {
				Ref<SphereMesh> gl;
				gl.instantiate();
				gl->set_radius(0.12f);
				gl->set_height(0.24f);
				_add_prim(arma, "Brilho_Lendario", gl, Vector3(0, 0.24f, 0), Color(1, 0.9f, 0.3f), 0.0f, true);
			}
			break;
		}
		case 3: { // PICARETA
			Ref<CylinderMesh> cabo;
			cabo.instantiate();
			cabo->set_top_radius(0.05f);
			cabo->set_bottom_radius(0.06f);
			cabo->set_height(1.0f);
			MeshInstance3D *cm = _add_prim(arma, "Cabo", cabo, Vector3(0, 0, 0), Color(0.55f, 0.34f, 0.14f));
			cm->set_rotation_degrees(Vector3(40, 0, 0));
			Ref<BoxMesh> cabeca;
			cabeca.instantiate();
			cabeca->set_size(Vector3(0.6f, 0.16f, 0.16f));
			MeshInstance3D *hd = _add_prim(arma, "Cabeca", cabeca, Vector3(0, 0.44f, 0.16f), Color(0.72f, 0.75f, 0.82f), 0.9f);
			hd->set_rotation_degrees(Vector3(40, 0, 0));
			if (brilho) {
				Ref<SphereMesh> gl;
				gl.instantiate();
				gl->set_radius(0.1f);
				gl->set_height(0.2f);
				_add_prim(arma, "Brilho_Lendario", gl, Vector3(0, 0.5f, 0.2f), Color(1, 0.9f, 0.3f), 0.0f, true);
			}
			break;
		}
		default: { // AR DOURADA
			Color ouro(1.0f, 0.78f, 0.18f);
			Ref<BoxMesh> corpo;
			corpo.instantiate();
			corpo->set_size(Vector3(0.12f, 0.2f, 0.55f));
			_add_prim(arma, "Corpo", corpo, Vector3(0, 0, 0.05f), ouro, 0.85f, brilho);
			Ref<CylinderMesh> cano;
			cano.instantiate();
			cano->set_top_radius(0.03f);
			cano->set_bottom_radius(0.04f);
			cano->set_height(0.45f);
			MeshInstance3D *cm = _add_prim(arma, "Cano", cano, Vector3(0, 0.02f, 0.52f), Color(0.2f, 0.2f, 0.24f), 0.85f);
			cm->set_rotation_degrees(Vector3(90, 0, 0));
			Ref<BoxMesh> cabo;
			cabo.instantiate();
			cabo->set_size(Vector3(0.1f, 0.24f, 0.13f));
			MeshInstance3D *cb = _add_prim(arma, "Cabo", cabo, Vector3(0, -0.18f, -0.18f), Color(0.2f, 0.14f, 0.08f));
			cb->set_rotation_degrees(Vector3(-15, 0, 0));
			Ref<BoxMesh> mira;
			mira.instantiate();
			mira->set_size(Vector3(0.04f, 0.06f, 0.14f));
			_add_prim(arma, "Mira", mira, Vector3(0, 0.15f, 0.05f), Color(0.1f, 0.1f, 0.12f), 0.7f);
			Ref<BoxMesh> pente;
			pente.instantiate();
			pente->set_size(Vector3(0.08f, 0.2f, 0.1f));
			MeshInstance3D *pm = _add_prim(arma, "Pente", pente, Vector3(0, -0.16f, 0.05f), ouro.darkened(0.15f), 0.85f);
			pm->set_rotation_degrees(Vector3(-8, 0, 0));
			if (brilho) {
				Ref<SphereMesh> gl;
				gl.instantiate();
				gl->set_radius(0.13f);
				gl->set_height(0.26f);
				_add_prim(arma, "Brilho_Lendario", gl, Vector3(0, 0.2f, 0), Color(1, 0.92f, 0.4f), 0.0f, true);
			}
			break;
		}
	}
	return arma;
}

// Pega todos os MeshInstance3D de sub-arvores cujo nome contem a palavra.
static void _collect_meshes_by_name(Node *p_node, const String &p_word, Vector<MeshInstance3D *> &r_out, int p_depth) {
	if (p_node == nullptr || p_depth > 6 || r_out.size() > 500) {
		return;
	}
	if (p_node->get_name().operator String().to_lower().contains(p_word)) {
		for (int i = 0; i < p_node->get_child_count(); i++) {
			Node *c = p_node->get_child(i);
			MeshInstance3D *m = Object::cast_to<MeshInstance3D>(c);
			if (m != nullptr) {
				r_out.push_back(m);
			}
			_collect_meshes_by_name(c, "", r_out, 99); // pega tudo da sub-arvore
		}
		return;
	}
	for (int i = 0; i < p_node->get_child_count(); i++) {
		_collect_meshes_by_name(p_node->get_child(i), p_word, r_out, p_depth + 1);
	}
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
		quick_row->add_child(_make_quick_button(TTR("BR 1km"), TTR("cria um mapa battle royale gigante de 1 km estilo ilha")));
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
			"1) Peça em português normal: \"cria um mapa battle royale gigante\" (ilha de 1 km!), \"cria um mapa pra pvp\", \"faz um personagem chibi\" (ou \"3 skins\"), \"cria um sniper lendário\", \"parede de metal\", \"baú de loot\", \"faz 3 árvores verdes\", \"monta um lobby\", \"pinta as casas de vermelho\"...\n\n"
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


	if (p_low.contains("battle royale") || p_low.contains("battleroyale") || p_low.contains("mapa br") ||
			p_low.contains("mapa gigante") || p_low.contains("mapa enorme") || p_low.contains("ilha") ||
			p_low.contains("mundo gigante")) {
		pending_action = ACTION_BR_MAP;
		_append_chat("NEX", TTR("ILHA BR GIGANTE! Vou montar na sua frente um mapa de 1 km no estilo cartoon limpo: grama bem verde, céu azul, vila, cidade com prédios, posto de gasolina, floresta fofa, estradas, 8 baús de loot dourado e a ZONA girando no meio. E vou apontar a câmera pra tudo aparecer na sua tela. Pode ser?"), NEX_PURPLE_LIGHT);
	} else if (p_low.contains("pinta") || p_low.contains("pintar") || p_low.contains("muda a cor") || p_low.contains("mudar a cor")) {
		pending_action = ACTION_RECOLOR;
		static const char *alvos[] = { "casa", "arvore", "carro", "personagem", "boneco", "bau", "parede", "predio", "muro", "plataforma", "moeda", "cristal", "rampa" };
		for (const char *a : alvos) {
			if (p_low.contains(String(a))) {
				pending_recolor_target = String(a);
				break;
			}
		}
		if (pending_recolor_target.is_empty()) {
			pending_recolor_target = "";
		}
		_append_chat("NEX", pending_recolor_target.is_empty() ?
				TTR("Repintar! Qual objeto? Me fala tipo \"pinta as casas de vermelho\" que eu mudo na hora, sem criar nada novo.") :
				vformat(TTR("Repintar! Vou mudar a cor de tudo que for \"%s\" que já tá na sua cena, sem criar nada novo. Pode ser?"), pending_recolor_target), NEX_PURPLE_LIGHT);
	} else if (p_low.contains("bau") || p_low.contains("loot") || p_low.contains("cofre")) {
		pending_action = ACTION_LOOT_CHEST;
		_append_chat("NEX", vformat(TTR("Baús de loot! Vou montar %d baú(s) dourado(s) brilhando estilo lendário. Pode ser?"), pending_count), NEX_PURPLE_LIGHT);
	} else if (p_low.contains("escudo") || p_low.contains("pocao") || p_low.contains("poção") || p_low.contains("numero de dano") || p_low.contains("efeito")) {
		pending_action = ACTION_EFFECT;
		_append_chat("NEX", TTR("Efeitos bonitos! Vou montar o kit: número de dano pulando, escudo azul em volta do boneco e poção de cura brilhando. Pode ser?"), NEX_PURPLE_LIGHT);
	} else if (p_low.contains("parede") || p_low.contains("muro") || p_low.contains("construcao de madeira") || p_low.contains("build")) {
		pending_action = ACTION_WALL;
		pending_wall_mat = _wall_mat_from_text(p_low);
		static const char *mats[3] = { "de MADEIRA (marrom)", "de TIJOLO (laranja)", "de METAL (cinza brilhante)" };
		_append_chat("NEX", vformat(TTR("Parede %s! Vou levantar com colisão, cor forte e limpa. Pode ser?"), String(mats[pending_wall_mat])), NEX_PURPLE_LIGHT);
	} else if (p_low.contains("mapa") || p_low.contains("mundo") || p_low.contains("arena") || p_low.contains("terreno")) {
		pending_action = ACTION_MAP;
		_append_chat("NEX", TTR("Mapa PvP! Vou montar na sua frente, passo a passo: chão com colisão, 4 paredes, luz do sol, céu bonito, árvores, rampa, plataformas, moedas e pontos de spawn. Vai ficar bonito. Pode ser?"), NEX_PURPLE_LIGHT);
	} else if (p_low.contains("colis") || p_low.contains("bloqueio")) {
		pending_action = ACTION_COLLISION;
		_append_chat("NEX", TTR("Colisão! Vou adicionar uma caixa sólida na cena (nada atravessa ela). Pode ser?"), NEX_PURPLE_LIGHT);
	} else if (p_low.contains("arma") || p_low.contains("tiro") || p_low.contains("weapon") || p_low.contains("gun") ||
			p_low.contains("picareta") || p_low.contains("fuzil") || p_low.contains("shotgun") ||
			p_low.contains("escopeta") || p_low.contains("sniper") || p_low.contains("rifle")) {
		pending_action = ACTION_WEAPON;
		pending_weapon_type = _weapon_type_from_text(p_low);
		pending_weapon_lend = p_low.contains("lendaria") || p_low.contains("lendária") || p_low.contains("lendario") || p_low.contains("brilho");
		static const char *tipos[4] = { "AR DOURADA", "SHOTGUN", "SNIPER", "PICARETA" };
		_append_chat("NEX", vformat(TTR("%s! Vou montar bonita, cor forte e estilo cartoon limpo%s. Pode ser?"), String(tipos[pending_weapon_type]), pending_weapon_lend ? TTR(", com BRILHO LENDÁRIO ligado") : TTR("")), NEX_PURPLE_LIGHT);
	} else if (p_low.contains("arvore") || p_low.contains("floresta")) {
		pending_action = ACTION_TREE;
		_append_chat("NEX", vformat(TTR("Árvores! Vou plantar %d árvore(s) de verdade: tronco de madeira e copa esférica %s. Pode ser?"), pending_count, pending_color_valid ? TTR("na cor que você pediu") : TTR("verde")), NEX_PURPLE_LIGHT);
	} else if (p_low.contains("personagem") || p_low.contains("boneco") || p_low.contains("jogador") || p_low.contains("npc") || p_low.contains("skin")) {
		pending_action = ACTION_CHAR;
		if (p_low.contains("skin") || p_low.contains("chibi") || p_low.contains("fofo")) {
			pending_count = 3;
		}
		_append_chat("NEX", pending_count >= 3 ?
				TTR("Boneco CHIBI! Cabeça grande, corpo pequeno, estilo cartoon fofo. Vou montar 3 SKINS de cores diferentes pra você escolher. Pode ser?") :
				TTR("Boneco CHIBI! Cabeça grande, corpo pequeno, olhinhos e cabelo, estilo cartoon fofo, com colisão pra mover com script depois. Pode ser?"), NEX_PURPLE_LIGHT);
	} else if (p_low.contains("casa") || p_low.contains("construcao") || p_low.contains("prédio") || p_low.contains("predio")) {
		pending_action = ACTION_HOUSE;
		_append_chat("NEX", TTR("Casa! Vou construir de verdade: chão, 4 paredes, porta, janelas iluminadas e telhado-> Pode ser?"), NEX_PURPLE_LIGHT);
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
	pending_brain_object = String();
	pending_count = 1;
	pending_scale = 1.0f;
	pending_color_valid = false;
	pending_weapon_type = 0;
	pending_wall_mat = 0;
	pending_weapon_lend = false;
	pending_recolor_target = String();

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
			_append_chat("NEX", TTR("Não posso copiar jogos de outras empresas pra te proteger de processo, mas posso criar um original AINDA MELHOR do mesmo estilo e tamanho. Me pede \"cria um mapa battle royale gigante\" que eu já faço na sua frente!"), NEX_PURPLE_LIGHT);
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

	// Pedidos de criacao: pergunta pro CEREBRO ONLINE (Kryno). Sem internet
	// ou sem chave, cai no entendimento local (offline nunca quebra).
	_brain_ask(txt);
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
			pending_action == ACTION_PLATFORM || pending_action == ACTION_GENERIC ||
			pending_action == ACTION_BR_MAP || pending_action == ACTION_WALL ||
			pending_action == ACTION_LOOT_CHEST || pending_action == ACTION_EFFECT);
	if (needs_3d && Object::cast_to<Node3D>(root) == nullptr) {
		_append_chat("NEX", TTR("Sua cena atual não é 3D. Toque no + pra criar uma \"Cena 3D\" (a que tem profundidade) e me pede de novo. HUD e Lobby eu consigo criar aqui mesmo."), NEX_PURPLE_LIGHT);
		pending_action = ACTION_NONE;
		return;
	}

	live_narrations.clear();
	live_kinds.clear();
	live_index = 0;
	live_total = 0;
	pending_have_real = false;

	// MODELO REAL: 3 niveis de escolha:
	// 1) pela acao pedida (arvore, casa, carro...), 2) pelo objeto que o
	// cerebro online entendeu (predio, fonte, parede...), 3) procurando
	// palavras no texto do pedido (modo offline).
	String low = _unaccent(pending_text.to_lower());
	bool use_real = (pending_action == ACTION_TREE || pending_action == ACTION_CHAR ||
			pending_action == ACTION_HOUSE || pending_action == ACTION_CAR ||
			pending_action == ACTION_WEAPON || pending_action == ACTION_COIN ||
			pending_action == ACTION_PLATFORM || pending_action == ACTION_GENERIC);
	const char *asset = nullptr;
	if (use_real) {
		asset = _asset_for(pending_action, low);
		if (asset == nullptr) {
			asset = _asset_by_object_word(pending_brain_object);
		}
		if (asset == nullptr && pending_action == ACTION_GENERIC) {
			asset = _asset_by_object_word(low);
		}
	}
	if (asset != nullptr) {
		pending_asset_file = String(asset);
		pending_asset_url = String(NEX_ASSETS_URL) + pending_asset_file;
	} else {
		pending_asset_file = String();
		pending_asset_url = String();
	}

	_append_chat("NEX", TTR("Fechado! Modo ao vivo ligado. Olha a cena 3D que eu vou montando..."), NEX_PURPLE_LIGHT);

	if (!pending_asset_url.is_empty()) {
		_queue_live(STEP_FETCH, TTR("Pegando na internet um MODELO DE VERDADE (3D real)..."));
	}

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
			_queue_live(STEP_GUN, TTR("Forjando a arma: corpo, cano, cabo e mira->.."));
			break;
		case ACTION_TREE:
			_queue_live(STEP_TREE, TTR("Plantando as árvores..."));
			break;
		case ACTION_CHAR:
			_queue_live(STEP_CHAR, TTR("Montando o personagem: corpo, cabeça, braços e pernas..."));
			break;
		case ACTION_HOUSE:
			_queue_live(STEP_HOUSE, TTR("Construindo a casa: paredes, porta, janelas e telhado->.."));
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
		case ACTION_BR_MAP:
			_queue_live(STEP_BR_TERRAIN, TTR("Nivelando 1 KM de grama bem verde com colisão e pintando o céu azul limpo..."));
			_queue_live(STEP_BR_TOWN, TTR("Construindo a VILINHA: 6 casinhas fofas..."));
			_queue_live(STEP_BR_CITY, TTR("Erguendo a CIDADE: 4 prédios com janelas acesas..."));
			_queue_live(STEP_BR_GAS, TTR("Montando o POSTO: garagens e carrões coloridos..."));
			_queue_live(STEP_BR_FOREST, TTR("Plantando a FLORESTA fofa..."));
			_queue_live(STEP_BR_ROADS, TTR("Abrindo as ESTRADAS cruzando a ilha..."));
			_queue_live(STEP_BR_LOOT, TTR("Espalhando 8 BAÚS DE LOOT dourados brilhando..."));
			_queue_live(STEP_BR_ZONE, TTR("Ligando a ZONA no centro da ilha..."));
			break;
		case ACTION_WALL:
			_queue_live(STEP_WALL, TTR("Levantando a parede sólida..."));
			break;
		case ACTION_LOOT_CHEST:
			_queue_live(STEP_CHEST, TTR("Montando os baús dourados lendários..."));
			break;
		case ACTION_EFFECT:
			_queue_live(STEP_EFFECT, TTR("Soltando os efeitos: dano, escudo e poção..."));
			break;
		case ACTION_RECOLOR:
			_queue_live(STEP_RECOLOR, TTR("Repintando o que já existe na cena..."));
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
			case ACTION_BR_MAP:
				_append_chat("NEX", TTR("PRONTINHO! ILHA BR de 1 KM montada: grama verde, céu azul, vilinha, cidade, posto, floresta, estradas, 8 baús de loot e a ZONA. A câmera já tá apontando pra ilha, olha a cena!"), NEX_PURPLE_LIGHT);
				break;
			case ACTION_RECOLOR:
				_append_chat("NEX", TTR("PRONTINHO! Tudo repintado."), NEX_PURPLE_LIGHT);
				break;
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
		// Aponta a camera do editor pro que foi criado (aparece NA TELA).
		_focus_scene();
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
	Node *ilha = nullptr;
	if (root != nullptr && root->has_node(NodePath("Ilha_BR"))) {
		ilha = root->get_node(NodePath("Ilha_BR"));
	}
	Color cor = pending_color_valid ? pending_color : NEX_PURPLE;

	switch (kind) {
		case STEP_FETCH: {
			// consome este passo agora; a continuacao vem no callback do download
			live_index++;
			Ref<DirAccess> da = DirAccess::open("user://");
			if (da.is_valid()) {
				da->make_dir_recursive("nex_assets");
			}
			_asset_save_path = "user://nex_assets/" + pending_asset_file;
			if (FileAccess::exists(_asset_save_path)) {
				// ja em cache (ja baixou antes) - usa direto
				pending_have_real = true;
				_live_next_step();
				return;
			}
			_asset_req = memnew(HTTPRequest);
			_asset_req->set_timeout(10.0);
			_asset_req->set_download_file(_asset_save_path);
			_asset_req->connect("request_completed", callable_mp(this, &NexAIChatPlugin::_on_asset_done));
			add_child(_asset_req);
			Error err = _asset_req->request(pending_asset_url);
			if (err != OK) {
				_asset_req->queue_free();
				_asset_req = nullptr;
				_live_next_step(); // segue sem modelo real
			}
			return;
		}
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
			// ARMA BONITA (v8): AR dourada / Shotgun / Sniper / Picareta,
			// cor forte e brilho lendario. Modelo real so se baixou e o
			// pedido nao pediu tipo especifico.
			String lowg = _unaccent(pending_text.to_lower());
			bool tipado = pending_weapon_type != 0 || pending_weapon_lend ||
					lowg.contains("picareta") || lowg.contains("shotgun") || lowg.contains("escopeta") ||
					lowg.contains("sniper") || lowg.contains("dourada") || lowg.contains("lendaria") || lowg.contains("lendária");
			if (pending_have_real && !tipado && _spawn_real(root, "Arma_Real", 2.0f)) {
				break;
			}
			bool lend = pending_weapon_lend || (pending_color_valid && (cor.r > 0.8f && cor.g > 0.6f && cor.b < 0.4f));
			Node3D *arma = _recipe_weapon_typed(pending_weapon_type, lend);
			arma->set_position(Vector3(1, 1, 0));
			arma->set_scale(Vector3(pending_scale, pending_scale, pending_scale));
			_add_node_live(root, arma, TTR("NEX: criar arma bonita"));
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
		if (pending_have_real && _spawn_real(root, "Arvore_Real", 3.0f)) {
			break;
		}

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
			// CHIBI (v8): cabeca grande, corpo pequeno. Modelo real so se
			// baixou E o pedido nao pediu cor/skins (a cor e a alma do chibi).
			String lowc = _unaccent(pending_text.to_lower());
			bool quer_chibi = pending_color_valid || pending_count >= 3 || lowc.contains("chibi") || lowc.contains("fofo") || lowc.contains("skin");
			if (pending_have_real && !quer_chibi && pending_count == 1 && _spawn_real(root, "Personagem_Real", 2.5f)) {
				break;
			}
			static const Color skins_corpo[3] = {
				Color(0.2f, 0.45f, 0.95f), Color(0.9f, 0.18f, 0.18f), Color(0.95f, 0.75f, 0.15f)
			};
			static const Color skins_cabelo[3] = {
				Color(0.1f, 0.15f, 0.4f), Color(0.35f, 0.18f, 0.05f), Color(0.95f, 0.85f, 0.3f)
			};
			int n = CLAMP(pending_count, 1, 3);
			Node3D *grupo = nullptr;
			if (n > 1) {
				grupo = memnew(Node3D);
				grupo->set_name("Skins");
			}
			for (int i = 0; i < n; i++) {
				Color corpo = pending_color_valid ? cor : skins_corpo[i % 3];
				CharacterBody3D *c = _recipe_chibi(corpo, skins_cabelo[i % 3]);
				if (n > 1) {
					c->set_name(String("Skin_") + (i == 0 ? "Azul" : (i == 1 ? "Vermelha" : "Dourada")));
					c->set_position(Vector3((i - 1) * 2.5f, 0, 0));
				}
				c->set_scale(Vector3(pending_scale, pending_scale, pending_scale));
				if (grupo != nullptr) {
					grupo->add_child(c);
				} else {
					_add_node_live(root, c, TTR("NEX: criar personagem chibi"));
				}
			}
			if (grupo != nullptr) {
				_add_node_live(root, grupo, TTR("NEX: criar skins chibi"));
			}
			break;
		}
		case STEP_HOUSE: {
		if (pending_have_real && _spawn_real(root, "Casa_Real", 8.0f)) {
			break;
		}

			Node3D *casa = _recipe_house(pending_color_valid ? cor : Color(0.85f, 0.8f, 0.7f));
			casa->set_scale(Vector3(pending_scale, pending_scale, pending_scale));
			_add_node_live(root, casa, TTR("NEX: construir casa"));
			break;
		}
		case STEP_CAR: {
		if (pending_have_real && _spawn_real(root, "Carro_Real", 4.0f)) {
			break;
		}

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
		if (pending_have_real && _spawn_real(root, "Moeda_Real", 1.5f)) {
			break;
		}

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
		if (pending_have_real && _spawn_real(root, "Plataforma_Real", 3.0f)) {
			break;
		}

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
		case STEP_BR_TERRAIN: {
			Node3D *br = memnew(Node3D);
			br->set_name("Ilha_BR");
			// grama bem verde (1 km x 1 km) com colisao
			StaticBody3D *grama = memnew(StaticBody3D);
			grama->set_name("Grama");
			Ref<PlaneMesh> pm;
			pm.instantiate();
			pm->set_size(Size2(1000, 1000));
			_add_prim(grama, "Grama_Visual", pm, Vector3(0, 0, 0), Color(0.36f, 0.78f, 0.32f));
			Ref<BoxShape3D> gsh;
			gsh.instantiate();
			gsh->set_size(Vector3(1000, 1, 1000));
			CollisionShape3D *gcs = memnew(CollisionShape3D);
			gcs->set_name("Colisao");
			gcs->set_shape(gsh);
			gcs->set_position(Vector3(0, -0.5f, 0));
			grama->add_child(gcs);
			br->add_child(grama);
			// sol quentinho
			DirectionalLight3D *sol = memnew(DirectionalLight3D);
			sol->set_name("Sol");
			sol->set_rotation_degrees(Vector3(-50, -35, 0));
			sol->set_light_color(Color(1.0f, 0.96f, 0.88f));
			sol->set_light_energy(1.15f);
			br->add_child(sol);
			// ceu azul estilizado limpo
			Ref<ProceduralSkyMaterial> psm;
			psm.instantiate();
			psm->set_sky_top_color(Color(0.25f, 0.55f, 1.0f));
			psm->set_sky_horizon_color(Color(0.7f, 0.87f, 1.0f));
			psm->set_ground_bottom_color(Color(0.3f, 0.65f, 0.3f));
			psm->set_ground_horizon_color(Color(0.55f, 0.8f, 0.55f));
			Ref<Sky> sky;
			sky.instantiate();
			sky->set_material(psm);
			Ref<Environment> env;
			env.instantiate();
			env->set_background(Environment::BG_SKY);
			env->set_sky(sky);
			env->set_ambient_source(Environment::AMBIENT_SOURCE_SKY);
			env->set_ambient_light_energy(1.1f);
			WorldEnvironment *we = memnew(WorldEnvironment);
			we->set_name("Ceu_Azul");
			we->set_environment(env);
			br->add_child(we);
			_add_node_live(root, br, TTR("NEX: criar ilha BR (terreno e ceu)"));
			break;
		}
		case STEP_BR_TOWN: {
			Node *parent = ilha != nullptr ? ilha : root;
			Node3D *vila = memnew(Node3D);
			vila->set_name("Vilinha");
			static const Vector3 pos_vila[6] = {
				Vector3(-120, 0, -100), Vector3(-95, 0, -100), Vector3(-120, 0, -75),
				Vector3(-95, 0, -75), Vector3(-107, 0, -122), Vector3(-108, 0, -55)
			};
			static const Color cores_vila[3] = {
				Color(0.95f, 0.9f, 0.8f), Color(0.9f, 0.75f, 0.6f), Color(0.85f, 0.88f, 0.95f)
			};
			for (int i = 0; i < 6; i++) {
				Node3D *casa = _recipe_house(cores_vila[i % 3]);
				casa->set_name("Casinha_" + String::num_int64(i + 1));
				casa->set_position(pos_vila[i]);
				casa->set_scale(Vector3(2.0f, 2.0f, 2.0f));
				vila->add_child(casa);
			}
			_add_node_live(parent, vila, TTR("NEX: vila da ilha BR"));
			break;
		}
		case STEP_BR_CITY: {
			Node *parent = ilha != nullptr ? ilha : root;
			Node3D *city = memnew(Node3D);
			city->set_name("Cidade");
			static const Vector3 pos_city[4] = {
				Vector3(120, 0, -120), Vector3(160, 0, -120), Vector3(120, 0, -160), Vector3(160, 0, -160)
			};
			static const float alt_city[4] = { 30, 45, 60, 38 };
			static const Color cores_city[4] = {
				Color(0.55f, 0.75f, 0.95f), Color(0.7f, 0.6f, 0.9f),
				Color(0.95f, 0.85f, 0.55f), Color(0.65f, 0.85f, 0.8f)
			};
			for (int i = 0; i < 4; i++) {
				Node3D *predio = memnew(Node3D);
				predio->set_name("Predio_" + String::num_int64(i + 1));
				float alt = alt_city[i];
				Ref<BoxMesh> corpo;
				corpo.instantiate();
				corpo->set_size(Vector3(14, alt, 14));
				_add_prim(predio, "Corpo", corpo, Vector3(0, alt / 2.0f, 0), cores_city[i], 0.1f);
				// faixas de janelas acesas (cartoon limpo)
				Ref<BoxMesh> faixa;
				faixa.instantiate();
				faixa->set_size(Vector3(14.1f, 1.2f, 14.1f));
				int n_faixas = (int)(alt / 8.0f);
				for (int j = 1; j <= n_faixas; j++) {
					_add_prim(predio, "Janelas_" + String::num_int64(j), faixa,
							Vector3(0, j * 8.0f - 4.0f, 0), Color(1.0f, 0.95f, 0.6f), 0.0f, true);
				}
				// topo
				Ref<BoxMesh> topo;
				topo.instantiate();
				topo->set_size(Vector3(16, 1.5f, 16));
				_add_prim(predio, "Topo", topo, Vector3(0, alt + 0.75f, 0), cores_city[i].darkened(0.2f));
				predio->set_position(pos_city[i]);
				city->add_child(predio);
			}
			_add_node_live(parent, city, TTR("NEX: cidade da ilha BR"));
			break;
		}
		case STEP_BR_GAS: {
			Node *parent = ilha != nullptr ? ilha : root;
			Node3D *posto = memnew(Node3D);
			posto->set_name("Posto");
			// 2 garagens
			static const Vector3 pos_gar[2] = { Vector3(120, 0, 120), Vector3(145, 0, 122) };
			for (int i = 0; i < 2; i++) {
				Node3D *gar = memnew(Node3D);
				gar->set_name("Garagem_" + String::num_int64(i + 1));
				Ref<BoxMesh> corpo;
				corpo.instantiate();
				corpo->set_size(Vector3(10, 5, 8));
				_add_prim(gar, "Corpo", corpo, Vector3(0, 2.5f, 0), Color(0.72f, 0.75f, 0.85f), 0.2f);
				Ref<BoxMesh> porta;
				porta.instantiate();
				porta->set_size(Vector3(4, 3.5f, 0.3f));
				_add_prim(gar, "Portao", porta, Vector3(0, 1.75f, 4.05f), Color(0.4f, 0.45f, 0.6f));
				Ref<BoxMesh> telhado;
				telhado.instantiate();
				telhado->set_size(Vector3(11, 0.8f, 9));
				_add_prim(gar, "Telhado", telhado, Vector3(0, 5.4f, 0), Color(0.85f, 0.3f, 0.2f));
				gar->set_position(pos_gar[i]);
				gar->set_scale(Vector3(2.0f, 2.0f, 2.0f));
				posto->add_child(gar);
			}
			// 3 carros coloridos
			static const Vector3 pos_car[3] = { Vector3(110, 0, 150), Vector3(130, 0, 155), Vector3(150, 0, 148) };
			static const Color cores_car[3] = { Color(0.9f, 0.15f, 0.15f), Color(0.15f, 0.45f, 0.9f), Color(0.95f, 0.8f, 0.1f) };
			for (int i = 0; i < 3; i++) {
				Node3D *car = _recipe_car(cores_car[i]);
				car->set_name("Carro_" + String::num_int64(i + 1));
				car->set_position(pos_car[i]);
				posto->add_child(car);
			}
			_add_node_live(parent, posto, TTR("NEX: posto da ilha BR"));
			break;
		}
		case STEP_BR_FOREST: {
			Node *parent = ilha != nullptr ? ilha : root;
			Node3D *floresta = memnew(Node3D);
			floresta->set_name("Floresta");
			static const Vector3 pos_flo[12] = {
				Vector3(-150, 0, 100), Vector3(-130, 0, 130), Vector3(-170, 0, 140),
				Vector3(-110, 0, 160), Vector3(-180, 0, 90), Vector3(-90, 0, 120),
				Vector3(-160, 0, 190), Vector3(-120, 0, 200), Vector3(-200, 0, 160),
				Vector3(-80, 0, 175), Vector3(-140, 0, 90), Vector3(-175, 0, 200)
			};
			for (int i = 0; i < 12; i++) {
				Node3D *arv = _recipe_tree(Color(0.22f, 0.72f, 0.25f));
				arv->set_name("Arvore_Fofa_" + String::num_int64(i + 1));
				arv->set_position(pos_flo[i]);
				float s = 1.6f + (i % 3) * 0.5f;
				arv->set_scale(Vector3(s, s, s));
				floresta->add_child(arv);
			}
			_add_node_live(parent, floresta, TTR("NEX: floresta da ilha BR"));
			break;
		}
		case STEP_BR_ROADS: {
			Node *parent = ilha != nullptr ? ilha : root;
			Node3D *estradas = memnew(Node3D);
			estradas->set_name("Estradas");
			Ref<BoxMesh> via;
			via.instantiate();
			via->set_size(Vector3(700, 0.2f, 12));
			_add_prim(estradas, "Via_X", via, Vector3(0, 0.1f, 0), Color(0.55f, 0.55f, 0.6f));
			Ref<BoxMesh> via2;
			via2.instantiate();
			via2->set_size(Vector3(12, 0.2f, 700));
			_add_prim(estradas, "Via_Z", via2, Vector3(0, 0.12f, 0), Color(0.55f, 0.55f, 0.6f));
			Ref<BoxMesh> linha;
			linha.instantiate();
			linha->set_size(Vector3(700, 0.05f, 0.8f));
			_add_prim(estradas, "Faixa_X", linha, Vector3(0, 0.24f, 0), Color(1.0f, 0.9f, 0.2f));
			Ref<BoxMesh> linha2;
			linha2.instantiate();
			linha2->set_size(Vector3(0.8f, 0.05f, 700));
			_add_prim(estradas, "Faixa_Z", linha2, Vector3(0, 0.26f, 0), Color(1.0f, 0.9f, 0.2f));
			_add_node_live(parent, estradas, TTR("NEX: estradas da ilha BR"));
			break;
		}
		case STEP_BR_LOOT: {
			Node *parent = ilha != nullptr ? ilha : root;
			Node3D *loot = memnew(Node3D);
			loot->set_name("Loot");
			static const Vector3 pos_loot[8] = {
				Vector3(-107, 0, -88), Vector3(140, 0, -140), Vector3(130, 0, 130),
				Vector3(-150, 0, 140), Vector3(0, 0, 60), Vector3(-40, 0, -60),
				Vector3(60, 0, 40), Vector3(0, 0, -220)
			};
			for (int i = 0; i < 8; i++) {
				Node3D *bau = _recipe_chest();
				bau->set_name("Bau_" + String::num_int64(i + 1));
				bau->set_position(pos_loot[i]);
				loot->add_child(bau);
			}
			// 2 pontos de spawn verdes brilhando
			for (int i = 0; i < 2; i++) {
				Ref<CylinderMesh> sp;
				sp.instantiate();
				sp->set_top_radius(3.0f);
				sp->set_bottom_radius(3.0f);
				sp->set_height(0.15f);
				_add_prim(loot, "Spawn_" + String::num_int64(i + 1), sp,
						Vector3(i == 0 ? -220.0f : 220.0f, 0.18f, 0), Color(0.1f, 0.9f, 0.3f), 0.0f, true);
			}
			_add_node_live(parent, loot, TTR("NEX: loot e spawns da ilha BR"));
			break;
		}
		case STEP_BR_ZONE: {
			Node *parent = ilha != nullptr ? ilha : root;
			Node3D *zona = memnew(Node3D);
			zona->set_name("Zona_BR");
			Ref<CylinderMesh> cil;
			cil.instantiate();
			cil->set_top_radius(60);
			cil->set_bottom_radius(60);
			cil->set_height(90);
			Ref<StandardMaterial3D> zmat;
			zmat.instantiate();
			zmat->set_albedo(Color(0.45f, 0.35f, 0.95f, 0.22f));
			zmat->set_transparency(StandardMaterial3D::TRANSPARENCY_ALPHA);
			zmat->set_feature(StandardMaterial3D::FEATURE_EMISSION, true);
			zmat->set_emission(Color(0.45f, 0.25f, 0.9f));
			zmat->set_emission_energy_multiplier(1.5f);
			MeshInstance3D *zm = memnew(MeshInstance3D);
			zm->set_name("Cupula");
			zm->set_mesh(cil);
			zm->set_material_override(zmat);
			zm->set_position(Vector3(0, 45, 0));
			zona->add_child(zm);
			_add_node_live(parent, zona, TTR("NEX: zona da ilha BR"));
			break;
		}
		case STEP_WALL: {
			StaticBody3D *wall = _recipe_wall(pending_wall_mat);
			Node3D *grupo = memnew(Node3D);
			grupo->set_name("Paredes_Construcao");
			for (int i = 0; i < pending_count; i++) {
				StaticBody3D *w = _recipe_wall(pending_wall_mat);
				w->set_position(Vector3((i - pending_count / 2.0f) * 5.0f, 0, 0));
				w->set_scale(Vector3(pending_scale, pending_scale, pending_scale));
				grupo->add_child(w);
			}
			memdelete(wall);
			_add_node_live(root, grupo, TTR("NEX: parede de construcao"));
			break;
		}
		case STEP_CHEST: {
			Node3D *grupo = memnew(Node3D);
			grupo->set_name("Baus_Loot");
			for (int i = 0; i < pending_count; i++) {
				Node3D *bau = _recipe_chest();
				bau->set_name("Bau_Loot_" + String::num_int64(i + 1));
				bau->set_position(Vector3((i - pending_count / 2.0f) * 2.5f, 0, 0));
				bau->set_scale(Vector3(pending_scale, pending_scale, pending_scale));
				grupo->add_child(bau);
			}
			_add_node_live(root, grupo, TTR("NEX: baus de loot"));
			break;
		}
		case STEP_EFFECT: {
			Node3D *fx = memnew(Node3D);
			fx->set_name("Efeitos");
			// 1) numero de dano pulando (cartoon)
			Label3D *dano = memnew(Label3D);
			dano->set_name("Dano_100");
			dano->set_text("100!");
			dano->set_font_size(48);
			dano->set_modulate(Color(1.0f, 0.55f, 0.1f));
			dano->set_outline_size(12);
			dano->set_modulate(Color(1.0f, 0.55f, 0.1f));
			dano->set_billboard_mode(StandardMaterial3D::BILLBOARD_ENABLED);
			dano->set_position(Vector3(0, 2.2f, 0));
			fx->add_child(dano);
			// 2) escudo azul em volta do boneco (translucido)
			Ref<SphereMesh> esc;
			esc.instantiate();
			esc->set_radius(1.1f);
			esc->set_height(2.2f);
			Ref<StandardMaterial3D> emat;
			emat.instantiate();
			emat->set_albedo(Color(0.25f, 0.55f, 1.0f, 0.3f));
			emat->set_transparency(StandardMaterial3D::TRANSPARENCY_ALPHA);
			emat->set_feature(StandardMaterial3D::FEATURE_EMISSION, true);
			emat->set_emission(Color(0.2f, 0.5f, 1.0f));
			emat->set_emission_energy_multiplier(1.2f);
			MeshInstance3D *escudo = memnew(MeshInstance3D);
			escudo->set_name("Escudo_Azul");
			escudo->set_mesh(esc);
			escudo->set_material_override(emat);
			escudo->set_position(Vector3(2.5f, 1.1f, 0));
			fx->add_child(escudo);
			// 3) pocao de cura roxa brilhando
			Node3D *pocao = memnew(Node3D);
			pocao->set_name("Pocao_Cura");
			Ref<CylinderMesh> frasco;
			frasco.instantiate();
			frasco->set_top_radius(0.22f);
			frasco->set_bottom_radius(0.3f);
			frasco->set_height(0.55f);
			_add_prim(pocao, "Frasco", frasco, Vector3(0, 0.28f, 0), Color(0.75f, 0.25f, 0.95f), 0.0f, true);
			Ref<CylinderMesh> gargalo;
			gargalo.instantiate();
			gargalo->set_top_radius(0.1f);
			gargalo->set_bottom_radius(0.12f);
			gargalo->set_height(0.2f);
			_add_prim(pocao, "Gargalo", gargalo, Vector3(0, 0.62f, 0), Color(0.9f, 0.9f, 0.95f));
			Ref<CylinderMesh> rolha;
			rolha.instantiate();
			rolha->set_top_radius(0.11f);
			rolha->set_bottom_radius(0.11f);
			rolha->set_height(0.08f);
			_add_prim(pocao, "Rolha", rolha, Vector3(0, 0.76f, 0), Color(0.6f, 0.4f, 0.2f));
			pocao->set_position(Vector3(5, 0, 0));
			fx->add_child(pocao);
			_add_node_live(root, fx, TTR("NEX: efeitos (dano, escudo, pocao)"));
			break;
		}
		case STEP_RECOLOR: {
			if (pending_recolor_target.is_empty() || !pending_color_valid) {
				_append_chat("NEX", TTR("Preciso saber o alvo e a cor, tipo \"pinta as casas de vermelho\"."), NEX_PURPLE_LIGHT);
				break;
			}
			Vector<MeshInstance3D *> alvos;
			if (root != nullptr) {
				_collect_meshes_by_name(root, pending_recolor_target, alvos, 0);
			}
			if (alvos.is_empty()) {
				_append_chat("NEX", vformat(TTR("Não achei nada chamado \"%s\" na cena ainda. Cria primeiro que depois eu pinto!"), pending_recolor_target), NEX_PURPLE_LIGHT);
				break;
			}
			EditorUndoRedoManager *undo_redo = EditorUndoRedoManager::get_singleton();
			undo_redo->create_action_for_history(TTR("NEX: repintar objetos"), EditorNode::get_editor_data().get_current_edited_scene_history_id());
			for (int i = 0; i < alvos.size(); i++) {
				Ref<Material> old_mat = alvos[i]->get_material_override();
				Ref<StandardMaterial3D> nm = _mat(pending_color, 0.15f);
				undo_redo->add_do_method(alvos[i], "set_material_override", nm);
				undo_redo->add_undo_method(alvos[i], "set_material_override", old_mat);
			}
			undo_redo->commit_action();
			_append_chat("NEX", vformat(TTR("Prontinho: %d peça(s) repintada(s) de %s!"), alvos.size(), pending_color.to_html(false)), NEX_PURPLE_LIGHT);
			break;
		}
		default: { // STEP_GENERIC
			if (pending_have_real && _spawn_real(root, "Objeto_Real", 3.0f)) {
				break;
			}
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

	if (kind != STEP_FETCH) {
		live_index++;
		// Pausa entre passos pra dar o efeito "fazendo ao vivo".
		get_tree()->create_timer(0.9f)->connect("timeout", callable_mp(this, &NexAIChatPlugin::_live_next_step));
	}
}

void NexAIChatPlugin::_focus_scene() {
	Node *root = EditorNode::get_singleton()->get_edited_scene();
	Node3D *r3 = Object::cast_to<Node3D>(root);
	if (r3 == nullptr) {
		return; // cena 2D (HUD/lobby): nada a focar
	}
	SubViewport *vp = EditorInterface::get_singleton()->get_editor_viewport_3d(0);
	if (vp == nullptr) {
		return;
	}
	Camera3D *cam = vp->get_camera_3d();
	if (cam == nullptr) {
		return;
	}
	// centro e tamanho aproximado da cena (varre filhos 3D).
	Vector3 centro = r3->get_global_position();
	Vector3 min = centro;
	Vector3 max = centro;
	int achados = 0;
	for (int i = 0; i < r3->get_child_count() && achados < 60; i++) {
		Node3D *c = Object::cast_to<Node3D>(r3->get_child(i));
		if (c == nullptr) {
			continue;
		}
		Vector3 p = c->get_global_position();
		min = min.min(p);
		max = max.max(p);
		achados++;
	}
	centro = (min + max) * 0.5f;
	Vector3 tam = (max - min).abs();
	float raio = MAX(tam.x, tam.z) * 0.75f + 10.0f;
	// posiciona a camera olhando pro centro (a 40 graus, bem cinematografico).
	Vector3 dir = Vector3(0.65f, 0.75f, 0.65f).normalized();
	cam->set_global_position(centro + dir * raio);
	cam->look_at(centro, Vector3(0, 1, 0));
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


// ---------------- CEREBRO ONLINE (Kryno no Base44) -------------------------

void NexAIChatPlugin::_brain_ask(const String &p_text) {
	Dictionary body;
	body["action"] = "nex";
	body["text"] = p_text;
	_brain_req = memnew(HTTPRequest);
	_brain_req->set_timeout(4.0);
	_brain_req->connect("request_completed", callable_mp(this, &NexAIChatPlugin::_on_brain_reply));
	add_child(_brain_req);
	Vector<String> headers;
	headers.push_back("Content-Type: application/json");
	Error err = _brain_req->request(String(NEX_BRAIN_URL), headers, HTTPClient::METHOD_POST, JSON::stringify(body));
	if (err != OK) {
		_brain_req->queue_free();
		_brain_req = nullptr;
		_classify_and_ask(_unaccent(p_text.to_lower()));
	}
}

void NexAIChatPlugin::_on_brain_reply(int p_result, int p_response_code, const PackedStringArray &p_headers, const PackedByteArray &p_body) {
	HTTPRequest *req = _brain_req;
	_brain_req = nullptr;
	if (req != nullptr) {
		req->queue_free();
	}
	String low = _unaccent(pending_text.to_lower());
	if (p_result != HTTPRequest::RESULT_SUCCESS || p_response_code != 200) {
		// sem internet/timeout: entendimento local (nunca quebra offline)
		_classify_and_ask(low);
		return;
	}
	String body_str = String::utf8((const char *)p_body.ptr(), p_body.size());
	Variant parsed = JSON::parse_string(body_str);
	if (parsed.get_type() != Variant::DICTIONARY) {
		_classify_and_ask(low);
		return;
	}
	Dictionary d = parsed;
	bool ok = false;
	if (d.has("ok")) {
		ok = static_cast<bool>(d["ok"]);
	}
	if (!ok) {
		// servidor sem chave de IA configurada (ou erro) -> cerebro local
		_classify_and_ask(low);
		return;
	}
	String type = static_cast<String>(d.get("type", String("create")));
	if (type == "answer") {
		String text = static_cast<String>(d.get("text", String()));
		_append_chat("NEX", text.is_empty() ? TTR("Entendi! Qualquer coisa me chama.") : text, NEX_PURPLE_LIGHT);
		return;
	}
	Dictionary cmd = static_cast<Dictionary>(d.get("command", Dictionary()));
	_apply_brain_command(cmd);
}

void NexAIChatPlugin::_apply_brain_command(const Dictionary &p_cmd) {
	String low = _unaccent(pending_text.to_lower());
	if (!p_cmd.has("object")) {
		_classify_and_ask(low);
		return;
	}
	String obj = static_cast<String>(p_cmd.get("object", String("generico")));
	pending_brain_object = obj;
	int64_t count = static_cast<int64_t>(p_cmd.get("count", 1));
	double scale = static_cast<double>(p_cmd.get("scale", 1.0));
	pending_count = CLAMP((int)count, 1, 10);
	pending_scale = CLAMP((float)scale, 0.5f, 3.0f);
	pending_color_valid = false;
	String color_word = static_cast<String>(p_cmd.get("color", String()));
	if (!color_word.is_empty()) {
		Color c;
		if (_color_from_text(color_word, c)) {
			pending_color = c;
			pending_color_valid = true;
		}
	}
	if (obj == "mapa_br") {
		pending_action = ACTION_BR_MAP;
	} else if (obj == "bau") {
		pending_action = ACTION_LOOT_CHEST;
	} else if (obj == "escudo" || obj == "pocao") {
		pending_action = ACTION_EFFECT;
	} else if (obj == "recolor") {
		pending_action = ACTION_RECOLOR;
		static const char *alvos[] = { "casa", "arvore", "carro", "personagem", "boneco", "bau", "parede", "predio", "muro", "plataforma", "moeda", "cristal", "rampa" };
		for (const char *a : alvos) {
			if (low.contains(String(a))) {
				pending_recolor_target = String(a);
				break;
			}
		}
	} else if (obj == "parede") {
		pending_action = ACTION_WALL;
		pending_wall_mat = _wall_mat_from_text(low);
	} else if (obj == "arma") {
		pending_action = ACTION_WEAPON;
		pending_weapon_type = _weapon_type_from_text(low);
		pending_weapon_lend = low.contains("lendaria") || low.contains("lendario") || low.contains("brilho");
	} else if (obj == "mapa") {
		pending_action = ACTION_MAP;
	} else if (obj == "arvore") {
		pending_action = ACTION_TREE;
	} else if (obj == "personagem") {
		pending_action = ACTION_CHAR;
	} else if (obj == "casa") {
		pending_action = ACTION_HOUSE;
	} else if (obj == "carro" || obj == "moto") {
		pending_action = ACTION_CAR;
	} else if (obj == "arma") {
		pending_action = ACTION_WEAPON;
	} else if (obj == "moeda") {
		pending_action = ACTION_COIN;
	} else if (obj == "plataforma") {
		pending_action = ACTION_PLATFORM;
	} else if (obj == "rampa") {
		pending_action = ACTION_RAMP;
	} else if (obj == "cristal") {
		pending_action = ACTION_CRYSTAL;
	} else if (obj == "hud") {
		pending_action = ACTION_HUD;
	} else if (obj == "lobby") {
		pending_action = ACTION_LOBBY;
	} else if (obj == "colisao") {
		pending_action = ACTION_COLLISION;
	} else {
		// parede, estrada, fonte, grama, nuvem, predio, tenda, inimigo,
		// bandeira e qualquer outro: generico + modelo real pelo objeto.
		pending_action = ACTION_GENERIC;
	}
	String confirm = static_cast<String>(p_cmd.get("confirm", String()));
	if (confirm.is_empty()) {
		confirm = TTR("Entendi certinho o que você quer!");
	}
	String extra = String();
	if (pending_action == ACTION_TREE || pending_action == ACTION_CHAR || pending_action == ACTION_HOUSE ||
			pending_action == ACTION_CAR || pending_action == ACTION_WEAPON || pending_action == ACTION_COIN ||
			pending_action == ACTION_PLATFORM || pending_action == ACTION_GENERIC) {
		extra = TTR(" Com internet eu ainda baixo um MODELO DE VERDADE da web!");
	}
	_append_chat("NEX", confirm + TTR(" Pode ser?") + extra, NEX_PURPLE_LIGHT);
	perm_row->show();
}

// ---------------- MODELO REAL: fim do download ------------------------------

void NexAIChatPlugin::_on_asset_done(int p_result, int p_response_code, const PackedStringArray &p_headers, const PackedByteArray &p_body) {
	HTTPRequest *req = _asset_req;
	_asset_req = nullptr;
	if (req != nullptr) {
		req->queue_free();
	}
	if (p_result == HTTPRequest::RESULT_SUCCESS && p_response_code >= 200 && p_response_code < 300 && FileAccess::exists(_asset_save_path)) {
		pending_have_real = true;
		_append_chat("NEX", TTR("Baixei o modelo de verdade! Colocando ele na sua cena..."), NEX_PURPLE_LIGHT);
	} else {
		if (FileAccess::exists(_asset_save_path)) {
			DirAccess::remove_absolute(_asset_save_path);
		}
		_append_chat("NEX", TTR("Não consegui baixar da internet agora; vou montar no meu estilo estilizado mesmo assim."), NEX_PURPLE_LIGHT);
	}
	_live_next_step();
}

bool NexAIChatPlugin::_spawn_real(Node *p_root, const String &p_base_name, float p_spacing) {
	String abs = ProjectSettings::get_singleton()->globalize_path(_asset_save_path);
	for (int i = 0; i < pending_count; i++) {
		Ref<GLTFState> state;
		state.instantiate();
		Ref<GLTFDocument> doc;
		doc.instantiate();
		if (doc->append_from_file(abs, state) != OK) {
			pending_have_real = false;
			return false;
		}
		Node *inst = doc->generate_scene(state);
		if (inst == nullptr) {
			pending_have_real = false;
			return false;
		}
		inst->set_name(pending_count > 1 ? (p_base_name + "_" + String::num_int64(i + 1)) : p_base_name);
		Node3D *n = Object::cast_to<Node3D>(inst);
		if (n != nullptr) {
			n->set_position(Vector3((i - pending_count / 2.0f) * p_spacing, 0, 0));
			if (pending_scale != 1.0f) {
				n->set_scale(Vector3(pending_scale, pending_scale, pending_scale));
			}
		}
		if (!_add_node_live(p_root, inst, TTR("NEX: colocar modelo de verdade"))) {
			memdelete(inst);
			pending_have_real = false;
			return false;
		}
	}
	return true;
}

void NexAIChatPlugin::_notification(int p_what) {
	// Nada por enquanto; mantido pra extensões futuras.
}
