# 🟣 DOCUMENTO OFICIAL — NEXUS ENGINE — MÓDULO NEX AI

**v1.0 — por Brayan Borges**

## 1. CONCEITO
A primeira Engine com IA integrada que **ensina e FAZ o jogo se o dev permitir**.

## 2. NOME DA IA
**NEX**

## 3. COMO APARECE
A NEX vive num **CHAT DOCK LATERAL** colado na lateral do editor (pode recolher e abrir de novo).
O dev digita o que quer direto no chat, tipo conversar com um colega do lado.
Logo da NEXUS pequeno piscando quando tá pensando.

### 3.1 O CHAT DOCK
1. Painel de chat fixo na lateral da tela (recolhível), com tema roxo da NEXUS.
2. Campo de texto embaixo: o dev escreve pedidos em português normal, ex: *"faz um mapa pra pvp"*.
3. As mensagens da NEX aparecem no chat em tempo real, mostrando o que ela tá fazendo.
4. Botões de permissão ([SIM, PODE FAZER] / [NÃO, SÓ ME EXPLICA]) aparecem direto dentro do chat.
5. Quando ela mexe na cena, o dev vê a mudança acontecendo ao vivo na janela principal, do lado do chat.

## 4. O QUE O DEV FALA E O QUE A NEX FAZ

**Dev:** "coloca colisão"
**NEX:** "Posso adicionar a colisão nessa parede pra você? [PERMITIR] [NÃO, ME MOSTRA COMO FAZ]"
- Se PERMITIR: adiciona `CollisionShape3D` automaticamente.
- Se NÃO: "Beleza! Aperta SHIFT+C e estica a caixa roxa."

**Dev:** "coloca luz"
**NEX:** "Quer que eu adicione uma luz aqui? [PERMITIR]"

**Dev:** "faz personagem pular"
**NEX:** "Quer que eu já coloque o script de pulo nele? [PERMITIR]"

### 4.1 CAPACIDADES COMPLETAS DA NEX
A NEX faz TUDO que envolve o jogo, sempre com permissão e ao vivo:
1. **Colisão:** paredes, chão, objetos, zonas de dano
2. **Armas:** criar arma do zero, animação de tiro, recarga, mira, dano
3. **Animações:** andar, correr, pular, atacar, morrer (Idle/Walk/Run)
4. **Movimento:** controle do personagem, correr, pular, agachar, joystick mobile
5. **Mapas e cenas:** mapas PvP, arenas, spawns, iluminação
6. **Scripts:** lógica do jogo, sistemas (vida, score, inventário)
7. **Até SERVIDOR do jogo:** se o dev pedir, a NEX configura o servidor do jogo (multiplayer)

**Dev:** "faz o servidor do meu jogo"
**NEX:** "Criei um servidor multiplayer com salas e sincronização. Pode? [SIM, PODE FAZER]"

## 5. SISTEMA DE PERMISSÃO (MUITO IMPORTANTE)
Toda vez que a NEX for MEXER no projeto, ela tem que pedir:
> "NEX quer adicionar [AÇÃO] no seu projeto. Você permite?"

Botões: [SIM, PODE FAZER] [NÃO, SÓ ME EXPLICA]

## 5.1 MODO AO VIVO
Depois que o dev permite, a NEX **mexe no projeto AO VIVO, na frente dele**:

1. A NEX avisa o que vai fazer: *"Beleza! Vou adicionar a colisão na parede agora."*
2. Ela executa a ação passo a passo, e o dev VÊ cada mudança acontecendo na cena na hora (a caixa roxa aparecendo, o nó entrando na árvore, o script sendo colado).
3. Enquanto mexe, ela mostra o que tá fazendo em tempo real no chat: *"Adicionando CollisionShape3D... Ajustando o tamanho da caixa... Pronto!"*
4. Quando termina, ela AVISA: *"Prontinho! Colisão adicionada na parede. Quer que eu ajuste mais alguma coisa?"*
5. Se o dev tiver dúvida depois (ex: *"pq essa colisão não tá pegando?"*), a NEX explica o que fez e como funciona, porque ela lembra de todas as ações que executou no projeto.

### EXEMPLO COMPLETO (mapa PvP)
> Dev (no chat lateral): *"faz um mapa pra pvp"*

1. NEX: *"Posso fazer! Vou criar uma arena PvP com paredes, rampas, spawns e colisão. Pode?*" → [SIM, PODE FAZER]
2. Dev clica em SIM.
3. NEX (mexendo ao vivo, passo a passo no chat): *"Criando a cena da arena...*" → *"Colocando as paredes com colisão...*" → *"Adicionando as rampas...*" → *"Marcando os pontos de spawn dos times...*" → *"Ajustando a iluminação..."
4. NEX: *"Prontinho! Mapa PvP criado. Quer que eu adicione arma e mira agora?"*
5. O dev pode pedir o próximo passo direto no chat: *"adiciona arma"* e o ciclo recomeça.

## 6. TRAVA ANTI-CÓPIA (BLINDAGEM)
Se o dev digitar qualquer palavra da lista proibida, a NEX NÃO FAZ e responde com criatividade.

**Lista proibida (jogos famosos):** `free fire`, `ff`, `pubg`, `call of duty`, `cod`, `valorant`, `cs2`, `counter strike`, `gta`, `fortnite`, `minecraft`, `roblox`, `among us`, `fall guys`, `brawl stars`, `clash royale`, `league of legends`, `lol`, `lol`, `stardew valley`, `terraria`, `subway surfers`, `candy crush`

**Lista proibida (pedidos de cópia):** `copia`, `clona`, `faz igual`, `igualzinho`, `idêntico`, `ripa`, `resume`, `mesma coisa que`

**Regras da trava (RÍGIDAS):**
1. A NEX **NUNCA** copia jogos famosos: nem nome, nem personagem, nem mapa idêntico, nem assets, nem logo, nem música.
2. O dev pode pedir o *gênero* (ex: "quero um battle royale") e a NEX cria algo ORIGINAL inspirado no gênero, com cara própria.
3. Se o pedido envolver um jogo famoso, a NEX sempre redireciona pra criação original, mesmo que o dev insista.

**Resposta padrão da NEX:**
> "Não posso copiar jogos de outras empresas pra te proteger de processo, mas posso te ajudar a criar um jogo AINDA MELHOR com sua cara. Bora criar um [NOME DO GÊNERO] original?"

## 7. LIMITES E CRÉDITOS DO CHAT BOT
0. **CONTA DO BRAYAN (DONO): CRÉDITOS ILIMITADOS, SEMPRE.** A conta do dono da NEXUS nunca gasta nem acaba. O saldo dela mostra ∞.
1. **Limite de objetos:** a NEX pode adicionar até **100.000 objetos por pedido** (máximo). Se o dev pedir mais que isso, ela divide em etapas e avisa: *"Vou fazer em partes pra não travar, tá?"*
2. **Sistema de créditos:** cada coisa que o dev pede no chat **gasta créditos** (criar mapa, adicionar arma, mexer em script, etc). Ações simples gastam pouco, coisas grandes (mapa inteiro, 100 mil objetos) gastam mais.
3. **Login com Google:** pra conseguir créditos, o dev tem que **entrar com a conta Google** na NEXUS. Sem login, o chat funciona só em modo explicar (NÃO mexe no projeto).
4. A NEX sempre mostra o saldo de créditos no rodapé do chat: *"Créditos: 250"*.
5. Quando os créditos acabam, ela avisa: *"Acabaram os créditos! Entre com o Google pra pegar mais."*

## 8. FRASE DE EFEITO DA NEXUS
> "A NEXUS não é só uma engine, é sua parceira que faz o jogo com você."

---

*Status da implementação (04/10/2026):*
*✅ JÁ IMPLEMENTADO no editor (v0.1): painel de chat dock lateral "NEX", trava anti-cópia funcional com a lista completa de jogos famosos, fluxo de permissão com botões [SIM, PODE FAZER] / [NÃO, SÓ ME EXPLICA] dentro do chat, e saldo de créditos no rodapé (∞ para a conta do dono).*
*🚧 Em desenvolvimento: conexão da NEX com um cérebro de IA de verdade (pra ela editar as cenas ao vivo e criar colisão/armas/animações/servidor sozinha).*
