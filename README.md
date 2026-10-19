# Viewfinder Mobile - V0.2 ALPHA (Android nativo: Kotlin + C++/OpenGL ES 3)

Projeto original e independente, inspirado no Viewfinder (Sad Owl Studios). Nenhum asset ou codigo do jogo original foi usado.

## Gerar o APK pelo celular (Termux + GitHub)
1. No GitHub crie um repositorio VAZIO (ex.: viewfinder-mobile) e um token (Settings > Developer settings > Tokens classic, marque "repo" e "workflow").
2. No Termux:
   termux-setup-storage
   pkg install -y git unzip
   cd ~ && unzip ~/storage/downloads/Viewfinder_V0.2_ALPHA.zip && cd Viewfinder_V0.2
   git init -b main && git add -A && git commit -m "V0.2 ALPHA"
   git remote add origin https://github.com/playerzin13mv/viewfinder-mobile.git
   git push -u origin main      (usuario = seu login, senha = o token)
3. Aguarde ~8 min na aba Actions. O APK aparece em Releases > "Viewfinder V0.2 ALPHA". Baixe e instale.

## Controles
- Esquerda da tela: joystick (aparece onde voce toca). Direita: arraste para olhar.
- FOTO: tira foto (guarda os objetos do enquadramento). Ate 4 fotos no topo.
- Toque numa foto para segura-la: surge o fantasma 3D + a imagem sobreposta para alinhar.
- COLAR: a foto vira realidade. TELEPORTE: vai ate onde a foto foi tirada. DESFAZER: recolhe a ultima colagem.
- Objetivo: chegar ao cristal amarelo no topo da parede laranja (dica: foto da escada, cole perto da parede).
