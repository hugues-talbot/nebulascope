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

*Lectures : Pitié, Kokaram & Dahyot 2007 (le transfert de distribution
itératif implémenté ici) ; Rabin et al. 2012 pour le point de vue
Wasserstein par tranches — références complètes en bibliographie.*
