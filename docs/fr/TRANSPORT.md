# Transport de couleurs — théorie et ajustement par étirement

Le transport de couleurs de NebulaScope transfère la *distribution* des
couleurs d'une image de référence sur l'image affichée. Il possède deux
modes d'application aux propriétés d'intégrité très différentes :

1. **Transport exact** — les pixels sont réécrits à travers une application
   de transport optimal empirique (une nouvelle entrée de liste ; la source
   n'est jamais modifiée).
2. **Ajustement par étirement** — aucun pixel n'est écrit : NebulaScope
   *ajuste l'étirement d'affichage* pour que la source **ressemble** au
   résultat transporté. La perte de données est nulle par construction.

Ce chapitre dérive les deux, et explique précisément pourquoi le mode 1
peut postériser et amplifier le bruit, et pourquoi le mode 2 ne le peut pas.

## 1. Le problème du transfert de distribution

Travaillons dans l'espace d'affichage : chaque pixel de la source est un
vecteur couleur $u \in [0,1]^3$ tiré d'une distribution $\mu_s$ ; les
pixels de la référence suivent $\mu_r$. On cherche une application
$T : [0,1]^3 \to [0,1]^3$ telle que si $u$ suit $\mu_s$, alors $T(u)$ suit
$\mu_r$ — ce qu'on note $T_\# \mu_s = \mu_r$ (« $T$ pousse $\mu_s$ vers
$\mu_r$ »). Parmi toutes ces applications, le transport optimal choisit
celle qui déplace le moins les couleurs, c'est-à-dire qui minimise
$\mathbb{E}\,\lVert T(u) - u \rVert^2$.

L'alignement des images est sans objet : seules comptent les deux
distributions de couleurs — les images n'ont besoin ni de se recouvrir, ni
d'avoir la même taille, ni même de montrer le même champ.

### 1.1 La dimension un se résout exactement

Pour des distributions scalaires de fonctions de répartition $F_s$ et
$F_r$, l'application optimale est le *réarrangement monotone* classique

$$T = F_r^{-1} \circ F_s ,$$

c'est-à-dire que la valeur au quantile $q$ de la source s'envoie sur la
valeur de la référence au même quantile. C'est exactement une
spécification d'histogramme, et c'est l'unique solution monotone.

### 1.2 Transport par tranches en dimension trois

Dans $\mathbb{R}^3$ il n'existe pas de forme close, mais la solution 1-D
peut être appliquée *par tranches* (le transfert de distribution itératif
de Pitié et al., de la même famille que les méthodes de Wasserstein par
tranches) : répéter, pour $k = 1 \dots K$,

1. tirer une rotation aléatoire $R_k \in SO(3)$ ;
2. projeter les deux nuages de points sur les trois axes tournés ;
3. appliquer le réarrangement monotone 1-D indépendamment sur chaque axe ;
4. appliquer la rotation inverse.

Chaque itération réduit l'écart entre la distribution courante et celle de
la référence ; après $K \approx 15$ passes, les marginales coïncident dans
suffisamment de directions pour que les distributions 3-D complètes
coïncident pour l'essentiel.

**Notes d'implémentation.** NebulaScope estime les applications sur au plus
$2 \times 10^5$ échantillons par image, restreints à la région visible de
chaque vue (les détails hors écran ne doivent pas orienter la
correspondance) et en excluant les pixels proches de la saturation (leurs
couleurs sont inappariables). Chaque application 1-D est tabulée sur 1024
nœuds de quantiles avec interpolation linéaire, et la suite des rotations
est initialisée de façon déterministe : les résultats sont reproductibles.
Une *force* $s \in [0,1]$ mélange le résultat avec l'original :
$u' = u + s\,(T(u) - u)$. Les paires d'images mono sautent le découpage en
tranches et utilisent directement le réarrangement 1-D.

## 2. Pourquoi le transport exact dégrade les données

Deux artefacts sont *inhérents* au passage des pixels à travers une
application empirique — aucun détail d'implémentation ne les supprime
entièrement.

### 2.1 Postérisation par échantillons ex æquo

L'application empirique est construite sur des données finies et
quantifiées. Là où de nombreux échantillons partagent une même valeur $v$
(les ex æquo sont garantis dès que la source a un jour été entière), la
fonction de répartition empirique saute : l'application envoie toute la
série d'ex æquo sur une même sortie, et la valeur d'entrée distincte
*suivante* atterrit un cran fini plus loin. L'histogramme transporté
développe des trous et des pics — visibles comme du *banding* dans les
dégradés doux. L'itération des tranches aggrave l'effet.

### 2.2 Amplification du bruit par la pente de l'application

Pour des densités lisses $f_s, f_r$, la différentiation de
$F_r(T(v)) = F_s(v)$ donne la pente locale de l'application 1-D :

$$T'(v) \;=\; \frac{f_s(v)}{f_r\!\big(T(v)\big)} .$$

Les petites perturbations (le bruit) sont multipliées par cette pente :
$\sigma_\text{sortie} \approx T'(v)\,\sigma_\text{entrée}$. Partout où la
distribution de la référence est localement *plus fine* que celle de la
source — c'est-à-dire là où la référence a plus de contraste, précisément
ce qu'on demandait — on a $T' > 1$ et le bruit est étiré avec le signal.
Épouser une référence contrastée amplifie donc *nécessairement* le bruit de
fond, sous n'importe quelle spécification d'histogramme exacte.

## 3. L'ajustement par étirement : épouser l'apparence, pas les pixels

L'affichage de NebulaScope applique, par canal $c$, un transfert
paramétrique lisse à la valeur brute $v$ :

$$D_c(v) \;=\; \mathrm{MTF}\!\big(W_c(v);\, m_c\big),$$

où le *fenêtrage* normalise et écrête,

$$W_c(v) \;=\; \operatorname{clip}_{[0,1]}
\left( \frac{\dfrac{v - \ell_c}{h_c - \ell_c} - b_c}{w_c - b_c} \right),$$

avec la plage de données $[\ell_c, h_c]$ et les points noir/blanc
$b_c, w_c$, et où la fonction de transfert des tons moyens est la courbe
rationnelle

$$\mathrm{MTF}(x; m) \;=\; \frac{(m-1)\,x}{(2m-1)\,x - m},
\qquad m_c = \frac{\mathrm{mid}_c - b_c}{w_c - b_c},$$

qui impose $\mathrm{MTF}(0)=0$, $\mathrm{MTF}(1)=1$ et
$\mathrm{MTF}(m)=\tfrac12$. Toute courbe de cette famille est lisse et
strictement monotone sur la fenêtre.

**L'ajustement.** Soient $t_i = T_c(u_i)$ les valeurs d'affichage
transportées d'un échantillon de pixels et $v_i$ les valeurs *brutes*
correspondantes. L'ajustement par étirement résout, indépendamment par
canal,

$$\min_{\,b_c,\ \mathrm{mid}_c,\ w_c}\;
\sum_i \omega_i \Big( D_c(v_i;\, b_c, \mathrm{mid}_c, w_c) - t_i \Big)^{2},
\qquad \omega_i = 0.05 + t_i,$$

un problème de moindres carrés pondérés borné à trois paramètres. La
*pondération par l'intensité* $\omega_i$ est essentielle : les pixels de
fond sont infiniment plus nombreux que les pixels de signal, et un
ajustement non pondéré épouserait le ciel en négligeant la nébuleuse.
Pondérer chaque résidu par la luminosité (avec plancher) de la cible fait
gouverner l'ajustement par les couleurs du signal, tandis que le petit
plancher maintient le point noir ancré. Les étoiles s'excluent au mieux à
la source — faites correspondre des rendus sans étoiles des deux images.
NebulaScope résout le problème par descente par coordonnées — quatre
passes de recherches linéaires par section dorée sur $b_c \in [0, w_c)$,
$w_c \in (b_c, 1]$, $\mathrm{mid}_c \in (b_c, w_c)$ — sur jusqu'à
$6 \times 10^4$ paires d'échantillons. Le résidu quadratique moyen par
canal est rapporté dans la barre d'état.

Le résultat n'est **qu'un état d'étirement** : il vit au même endroit que
n'importe quel Auto STF, se compose avec copier/coller et *Appliquer à
tout*, persiste dans le fichier annexe, et les données sous-jacentes
restent intouchées.

### 3.1 Garanties

- **Perte de données nulle** — rien n'est écrit ; le « résultat » est une
  vue.
- **Aucune postérisation** — la courbe ajustée est lisse et strictement
  monotone ; il n'existe aucune marche empirique sur laquelle des ex æquo
  pourraient s'effondrer.
- **Comportement du bruit maîtrisé** — la pente de la courbe est le
  $D_c'(v)$ paramétrique lisse, exempt des pentes localement extrêmes
  qu'un rapport de densités empiriques peut produire.

### 3.2 Seconde étape : ajuster les réglages de couleur

En option, une seconde étape réduit l'écart de séparabilité. Les courbes
par canal étant figées, NebulaScope ajuste les quatre réglages de couleur
inter-canaux — température, teinte (*tint*), rotation de teinte (*hue*),
saturation — en minimisant l'erreur pondérée par l'intensité sur les
*triplets* RVB complets :

$$\min_{\theta}\; \sum_i \omega_i\,
\big\lVert A_\theta\big(D(v_i)\big) - T(u_i) \big\rVert^2 ,$$

où $A_\theta$ est l'opérateur d'ajustement. Ces quatre paramètres
mélangent les canaux — précisément ce que font les rotations du transport
par tranches et ce que des courbes séparables ne peuvent pas faire — de
sorte que des cibles à rotation de teinte hors de portée de l'étape 1
deviennent atteignables. La barre d'état rapporte l'erreur RMS globale
après cette étape ; l'annulation étant à une touche, essayer les deux
variantes ne coûte rien.

### 3.3 L'écart d'approximation

La famille des étirements est *séparable* : trois courbes monotones
indépendantes. L'application de transport optimal ne l'est en général pas —
ses rotations mélangent les canaux, ce qui lui permet de faire tourner les
teintes. La projection de $T$ sur la famille séparable reproduit donc
fidèlement l'équilibre global des couleurs et la tonalité, mais ne peut
exprimer une rotation de teinte. L'erreur RMS par canal rapportée
quantifie cet écart pour votre paire d'images ; quand il compte, le mode
exact reste à une case à cocher.

## 4. En pratique

- Boîte de dialogue : **Outils ▸ Transporter les couleurs d'une
  référence…**, cochez *Appliquer comme ajustement d'étirement*. Scripts :
  `transport <ligne> [force%] stretch`.
- La force s'applique *avant* l'ajustement — l'ajustement vise le résultat
  mélangé.
- **Le cadrage est le masque de pertinence.** Les deux distributions ne
  sont estimées que sur ce qui est à l'écran : la source sur le rectangle
  visible de la vue active, la référence sur le rectangle visible de *sa*
  cellule quand elle y est affichée (les rotations sont défaites pour que
  les bordures d'expansion ne contribuent jamais ; les pixels saturés
  ≥ 0,98 sont écartés des deux côtés). Une référence qui n'est qu'une
  ligne de liste — affichée dans aucune cellule — est estimée sur son
  image entière. Pour contrôler ce qui juge l'appariement, affichez donc
  la référence dans une cellule et cadrez la région qui compte : éloignez
  un gradient ou un bord de champ du cadre, et le transport ne les verra
  jamais.
- Les deux modes sont **annulables** : le mode exact comme ajout d'entrée
  de liste, l'ajustement par étirement comme changement d'état d'étirement
  (⌘Z restaure l'étirement précédent).
- L'ajustement absorbe l'affichage courant (ajustements compris) dans de
  nouvelles valeurs Linéaire noir/médian/blanc et remet les curseurs
  d'ajustement à zéro : ce que vous voyez juste après est la
  correspondance ajustée elle-même.
- Juger l'erreur RMS : les valeurs sont en unités d'affichage sur $[0,1]$ ;
  l'expérience suggère qu'un ajustement sous $\sim 0{,}02$ est visuellement
  convaincant, tandis que des résidus plus grands indiquent en général que
  la référence exigeait une rotation de teinte.

- **La référence n'a pas besoin de montrer le même champ.** Le transport
  apparie des *distributions*, jamais des pixels : toute image au contenu
  du même *genre* est une référence légitime — deux parties différentes
  d'un même rémanent de supernova (rubans OIII et Hα sur un champ
  d'étoiles) ont des distributions de couleurs semblables alors qu'aucun
  détail ne coïncide. Cas de terrain : les NGC 6960 et NGC 6992 d'un
  collaborateur, rendues avec deux STF différentes, ont été rendues
  identiques en couleur à partir de deux JPEG seulement — sans données
  linéaires, sans accès aux pixels, champs différents. C'est l'ajustement
  non destructif qui rend cela sûr : une courbe monotone par canal plus un
  déplacement global de teinte/saturation ne peut inventer de structure là
  où les distributions divergent réellement, et « rendre semblables » se
  dégrade gracieusement en « aussi semblables qu'une transformation
  d'affichage le permet ». Et comme le résultat est un bloc d'affichage
  (§6), l'appariement est un fichier que l'on peut rendre à son auteur.

## 5. Annexe : fonctions d'affichage importées et recalage de Möbius

Quand un XISF porte l'étirement d'écran enregistré par l'application
productrice (l'élément `DisplayFunction` — la STF de PixInsight),
NebulaScope l'applique à la première visualisation. Un désaccord
structurel doit être résolu : PI définit sa STF sur le *conteneur*
normalisé $[0,1]$, si bien que son point blanc (typiquement $h = 1$) peut
se trouver à une coordonnée fenêtrée $1/k$ — souvent $80$ à $300\times$ —
au-delà du maximum réel des données, alors que chaque contrôle de
NebulaScope (tracé, poignées, champs de valeur, barre de couleurs) est
paramétré sur la plage des données. Importée telle quelle, la poignée
blanche se retrouverait à $1/k$ largeurs de tracé hors écran.

**Le fait structurel clé.** La fonction de transfert des tons moyens

$$\mathrm{MTF}(x; m) \;=\; \frac{(m-1)\,x}{(2m-1)\,x - m}$$

est une *transformation de Möbius* fixant $0$ et $1$ ; réciproquement, la
famille MTF est exactement l'ensemble des applications de Möbius monotones
à ces deux points fixes — deux contraintes sur un groupe à trois
paramètres laissent un degré de liberté, le pivot $m$ (où la sortie vaut
$\tfrac12$).

**Restriction et renormalisation.** Soit $D(t) = \mathrm{MTF}(t; m)$ la
courbe importée sur sa propre fenêtre, et $k \in (0,1)$ la coordonnée
fenêtrée du maximum des données. Restreignons aux données et rééchelonnons
pour que le maximum des données s'affiche au niveau $S$ :

$$g(t') \;=\; \frac{D(k\,t')}{D(k)}, \qquad t' \in [0,1].$$

Mettre l'argument à l'échelle ($t = k\,t'$) est de Möbius ; diviser par la
constante $D(k)$ est de Möbius ; la composée d'applications de Möbius est
de Möbius — et par construction $g(0)=0$, $g(1)=1$. Donc $g$ *est encore
une MTF* : la famille est fermée par restriction-renormalisation, raison
pour laquelle une forme close existe. Son pivot se retrouve par l'inverse
en forme close

$$\mathrm{MTF}^{-1}(y; m) \;=\; \frac{m\,y}{(2m-1)\,y - m + 1},
\qquad m' \;=\; \frac{\mathrm{MTF}^{-1}\!\big(\tfrac{D(k)}{2};\, m\big)}{k}.$$

Aucun ajustement, aucun échantillonnage, aucune approximation : la courbe
recalée égale l'originale sur toute valeur présente dans les données.

**Couleur : un niveau commun.** Rééchelonner chaque canal par son *propre*
point terminal $D_c(k_c)$ referait silencieusement la balance des blancs —
défaisant précisément l'étalonnage (p. ex. SPCC) que le fichier
enregistre. NebulaScope utilise donc un unique niveau commun

$$S \;=\; \max_c D_c(k_c),$$

qui place le nouveau blanc du canal $c$ en $\mathrm{MTF}^{-1}(S;\, m_c)$ :
le blanc du canal le plus brillant tombe exactement sur son maximum de
données (toutes les poignées sur le tracé) et chaque valeur affichée est
divisée par le *même* $S$,

$$D'_c(v) \;=\; \frac{D_c(v)}{S}\quad \text{pour toute donnée } v,$$

de sorte que les rapports entre canaux — teinte et balance étalonnée —
sont préservés *exactement*. Le seul écart avec l'écran de l'application
productrice est un facteur de luminosité uniforme $1/S$ (typiquement moins
de $10\,\%$), monotone et symétrique entre canaux.

*Lectures : Pitié, Kokaram & Dahyot 2007 (le transfert de distribution
itératif implémenté ici) ; Rabin et al. 2012 pour le point de vue
Wasserstein par tranches — références complètes en bibliographie.*

## 6. Annexe : le bloc d'affichage — un format d'apparence ouvert et reproductible

Tout ajustement d'étirement finit en nombres, et NebulaScope les écrit.
**Enregistrer les annotations et l'affichage** stocke l'état d'affichage
complet dans le fichier annexe de l'image (`<image>_annotation.json`) sous
une clé `display`. Le bloc est du JSON simple avec un schéma déclaré, chaque
champ correspond à une équation en forme close de ce livre, et une
implémentation de référence autonome (`tools/render_sidecar.py`, NumPy
seul, sans aucun code NebulaScope) reproduit le rendu de NebulaScope à
partir de lui — vérifié en intégration continue à quelques ULP float32 près
sur chaque pixel (`tests/conformance/`). C'est ce qui rend un étirement
*explicable* (lire le bloc, savoir exactement ce qui a été fait) et
*reproductible dans d'autres logiciels* (implémenter six courtes
équations).

### 6.1 Le bloc

```json
"display": {
  "schema": 1,
  "fn": "ghs",                       // linear | log | asinh | ghs
  "count": 3,                        // canaux pour lesquels l'état a été fait
  "channels": [                      // R, V, B (l'indice 0 sert au mono)
    { "lo": 0.0012, "hi": 0.9840,    // fenêtre de données (unités brutes)
      "black": 0.031, "mid": 0.118, "white": 1.0 },   // normalisés dans [lo,hi]
    { ... }, { ... }
  ],
  "ghs":    { "D": 1.6, "b": 6.0, "SP": 0.18, "LP": 0.0, "HP": 1.0 },
  "cmap": "gray", "cmapInvert": false, "cmapSplit": false, "split": 0.25,
  "adjust": { "blackpoint": 0, "whitepoint": 1, "shadows": 0,
              "highlights": 0, "brightness": 0, "contrast": 0, "gamma": 1,
              "temperature": 0, "tint": 0, "hue": 0,
              "saturation": 0, "vibrance": 0 }
}
```

`schema` est la version de *ce* bloc (indépendante de la `version` propre
du fichier annexe) ; un lecteur doit refuser un schéma plus récent que
celui qu'il connaît. Les énumérations sont stockées par nom, jamais par
entier. `black`/`mid`/`white` et les `SP`/`LP`/`HP` de GHS sont des
coordonnées de fenêtre normalisées et **peuvent sortir de $[0,1]$** (point
noir sous le minimum des données, point de symétrie hors de la fenêtre) :
la chaîne ci-dessous est définie pour toute valeur réelle, et une
implémentation conforme ne doit pas les borner.

### 6.2 La chaîne, par canal $c$ et valeur brute $v$

**Fenêtre.** Les poignées noir/milieu/blanc du canal vivent en
*coordonnées de fenêtre normalisées* sur $[\mathrm{lo}_c, \mathrm{hi}_c]$
— c'est ce qui rend un état portable entre images de plages de données
différentes :

$$t \;=\; \operatorname{clamp}_{[0,1]}\!\left(
  \frac{\dfrac{v-\mathrm{lo}_c}{\mathrm{hi}_c-\mathrm{lo}_c} - \mathrm{black}_c}
       {\mathrm{white}_c-\mathrm{black}_c}\right).$$

**Transfert** $T(t)$, l'un de :

- `linear`, `log`, `asinh` — une forme de base suivie de la fonction de
  transfert des tons moyens (§5) de pivot
  $m_c = (\mathrm{mid}_c-\mathrm{black}_c)/(\mathrm{white}_c-\mathrm{black}_c)$,
  borné à $[0{,}001,\ 0{,}999]$ :
  $$T(t) = \mathrm{MTF}\big(s(t);\, m_c\big),\qquad
    s(t) = \begin{cases} t & \text{linear}\\
      \ln(1+500t)/\ln 501 & \text{log}\\
      \operatorname{asinh}(50t)/\operatorname{asinh}50 & \text{asinh}\end{cases}$$
- `ghs` — une courbe *maîtresse* partagée par tous les canaux : l'intégrale
  cumulée normalisée de la pente d'étirement local
  $$\sigma(x) = D_e\,\big(1 + b\,D_e\,|x-\mathrm{SP}|\big)^{-(1+1/b)}
    \quad (b>0;\ \text{logarithmique } D_e/(1+|b|D_e|x-\mathrm{SP}|) \text{ pour } b<0,\
    \text{exponentielle } D_e e^{-D_e|x-\mathrm{SP}|} \text{ pour } b=0),$$
  avec $D_e = e^{D}-1$ et $x$ borné à $[\mathrm{LP},\mathrm{HP}]$ avant
  d'évaluer $\sigma$ (zones de protection linéaires) ;
  $T(t) = \int_0^t \sigma \,/\, \int_0^1 \sigma$. NebulaScope l'évalue sur
  une grille trapézoïdale de 4096 points et interpole linéairement entre
  les nœuds ; une implémentation conforme fait de même, ce qui donne un
  accord au niveau de l'ULP plutôt qu'un simple accord visuel.

**Ajustements de tonalité** (monotones, par canal, composés dans $T$), dans
cet ordre : re-fenêtrage point noir/point blanc, puis
$y \mathrel{+}= \mathrm{shadows}\cdot 2y(1-y)^2$,
$y \mathrel{+}= \mathrm{highlights}\cdot 2y^2(1-y)$,
$y \mathrel{+}= \mathrm{brightness}/2$,
$y = \tfrac12 + (y-\tfrac12)\tan\!\big((\mathrm{contrast}+1)\tfrac{\pi}{4}\big)$,
bornage, puis $y^{1/\gamma}$.

**Ajustements de couleur** (inter-canaux, RVB seulement), dans cet ordre :
gains de balance des blancs
$R\,(1+0{,}30\,\mathrm{temp}+0{,}15\,\mathrm{tint}),\;
 G\,(1-0{,}30\,\mathrm{tint}),\;
 B\,(1-0{,}30\,\mathrm{temp}+0{,}15\,\mathrm{tint})$ ;
la matrice standard de rotation de teinte à luminance préservée (les
coefficients `hue-rotate` de SVG/CSS) de `hue` degrés ; puis saturation
autour de la luma Rec.\,709 $Y$ : $C = Y + (C-Y)\,f$ avec
$f = (1+\mathrm{saturation})\,(1+\mathrm{vibrance}\,(1-s))$, $s$ la
saturation TSV du pixel. Bornage à $[0,1]$.

**Sortie.** Le résultat est le rendu Float32 [0,1] (ce que *Enregistrer
l'image étirée sous…* grave). L'image écran 8 bits en est l'arrondi sur
255 niveaux — NebulaScope ajoute à l'écran un tramage triangulaire de
±1 LSB, cosmétique d'affichage qui ne fait délibérément *pas* partie du
format.

### 6.3 Lire un ajustement de transport

Un transport de couleurs non destructif (§3) n'est rien d'autre qu'un bloc
d'affichage : les `black/mid/white` par canal qu'il a résolus, plus, quand
l'ajustement couleur était activé, le vecteur `adjust`. Deux ajustements se
comparent comme du JSON, et la différence se localise — fenêtrage contre
teinte contre saturation — ce qui renseigne sur la façon dont les deux
images diffèrent, et pas seulement sur une recette.

### 6.4 Portée et hors-champ de l'implémentation de référence

`render_sidecar.py` couvre les quatre fonctions de transfert, le fenêtrage
par canal, tous les ajustements de tonalité et de couleur, mono et RVB.
Les palettes en fausses couleurs (`cmap` autre que `gray`, ou les
modificateurs inversion/scission) sont des tables 8 bits définies par
points de contrôle dans `src/core/Colormap.cpp`, qui reste la référence à
leur sujet ; une image mono avec palette active passe par le même
étirement puis est indexée dans cette table.
