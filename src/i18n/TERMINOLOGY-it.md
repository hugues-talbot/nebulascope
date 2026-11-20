# Italian interface: terminology table

Status: **first native review received (2026-11-20).** The Italian interface
strings (`nebulascope_it.ts`) are written against this table. English is the
source language; the French column is the existing translation, given for
reference.

Scope: the application interface only. Script commands (`.nsc`),
command-line output and script messages stay in English, as they do in
French. The manual stays English and French.

## 1. Decisions and open questions

Reviewed by a native speaker who is an astronomer:

| Term | Decision |
|---|---|
| Stretch | **stretch**, masculine, invariable: "lo stretch", "Copia lo stretch" |
| Debayer | **demosaicizzazione** (not "debayerizzazione") |
| RA/Dec | **AR/Dec** |
| Kernel | **kernel**, left in English |
| Colormap | **mappa colori** (not "mappa di colori") |
| View menu | **Visualizza** |
| Fitted stars | **stelle modellate**. "Stelle fittate" is spoken jargon and does not suit an interface. |

Still open. The string in use is the proposal; each can be changed freely.

| # | Question | In use | Alternative |
|---|---|---|---|
| 1 | Voice of commands and messages: does the application address the user informally, or impersonally? | **informal imperative**: "Apri…", "Scegli le due immagini." | impersonal infinitive: "Scegliere le due immagini." |
| 2 | The keep mark of the image list. Each image in the list has a tick box. A ticked image is one to keep, and commands then move or remove the ticked or the unticked ones. It is a mark on an item, not a filter being active. "Spuntata" was judged too informal for software. | **contrassegnata** / "con contrassegno", "senza contrassegno", "Contrassegna le selezionate" | marcata; spuntata |
| 3 | "Image" and "frame" | **immagine** everywhere, including "Move Frames" | frame for the culling commands |
| 4 | FITS "header" | **intestazione** | header |
| 5 | Position angle, short form | **AP** (angolo di posizione) | PA |
| 6 | "Sidecar" file | **file sidecar** | file associato |
| 7 | DS9 "bias" (with contrast, on the mouse) | **bias**, left in English | scostamento |
| 8 | "Match Views" (align two views on a common feature) | **Abbina le viste** | Allinea le viste |
| 9 | Box annotation | **riquadro** | rettangolo |
| 10 | "Plate solution" | **soluzione astrometrica** | plate solving, left in English |
| 11 | "Starless" / "stars-only" images | **senza stelle** / **solo stelle** | starless, left in English |
| 12 | Quotation marks around file names | **«nome»** without inner spaces | “nome” |
| 13 | "Delivered by proxy" (a deconvolved starless image whose PSF is verified on its starry counterpart) | **tramite un sostituto** | per procura; proxy, left in English |

## 2. Menus and standard commands

| English | French | Italian |
|---|---|---|
| File / Edit / View / Image / Stretch / Tools / Help | Fichier / Édition / Affichage / Image / Étirement / Outils / Aide | File / Modifica / Visualizza / Immagine / Stretch / Strumenti / Aiuto |
| Open… / Open Recent | Ouvrir… / Ouvrir récent | Apri… / Apri recenti |
| Save / Save … As… | Enregistrer / Enregistrer … sous… | Salva / Salva … come… |
| Save Data As… | Enregistrer les données sous… | Salva i dati come… |
| Save Stretched As… | Enregistrer l'image étirée sous… | Salva l'immagine con stretch come… |
| Export View As… | Exporter la vue sous… | Esporta la vista come… |
| Export Zoomed Region As… | Exporter la région zoomée sous… | Esporta la regione ingrandita come… |
| Import / Export | Importer / Exporter | Importa / Esporta |
| Quit | Quitter | Esci |
| Undo / Redo | Annuler / Rétablir | Annulla / Ripristina |
| Reset | Réinitialiser | Reimposta |
| Close | Fermer | Chiudi |
| Preferences… | Préférences… | Preferenze… |
| Configure Shortcuts… | Configurer les raccourcis… | Configura le scorciatoie… |
| About NebulaScope | À propos de NebulaScope | Informazioni su NebulaScope |
| Language / System default | Langue / Langue du système | Lingua / Lingua di sistema |
| Takes effect at the next launch | Prend effet au prochain lancement | Ha effetto al prossimo avvio |
| Apply to All | Appliquer à tout | Applica a tutte |
| None / off / auto | Aucun / désactivé / auto | Nessuno / disattivato / auto |
| … failed | Échec de … | … non riuscita / non riuscito |
| Could not read / write %1 | Impossible de lire / d'écrire %1 | Impossibile leggere / scrivere %1 |

## 3. Views and navigation

| English | French | Italian |
|---|---|---|
| view | vue | vista |
| Split View | Diviser la vue | Dividi la vista |
| Single / side by side / stacked | Simple / côte à côte / empilées | Singola / affiancate / impilate |
| Rows / Columns | Lignes / Colonnes | Righe / Colonne |
| Zoom to Fit | Ajuster à la vue | Adatta alla vista |
| Zoom to Width | Ajuster à la largeur | Adatta alla larghezza |
| Zoom In / Zoom Out (Fine) | Zoom avant / arrière (fin) | Ingrandisci / Riduci (fine) |
| Fullscreen | Plein écran | Schermo intero |
| Image Only | Image seule | Solo immagine |
| Overlay Panels | Panneaux en surimpression | Pannelli in sovrimpressione |
| Hide Scrollbars | Masquer les barres de défilement | Nascondi le barre di scorrimento |
| Coordinate Grid | Grille de coordonnées | Griglia di coordinate |
| Values in All Views | Valeurs dans toutes les vues | Valori in tutte le viste |
| Match Views: Pick a Feature | Apparier les vues : pointer un détail | Abbina le viste: scegli un dettaglio |
| feature pair | paire de détails | coppia di dettagli |
| Views calibration-linked / View unlinked | Vues liées par calibration / Vue déliée | Viste collegate per calibrazione / Vista scollegata |
| visible region | région visible | regione visibile |

## 4. Stretch, histogram and adjustments

| English | French | Italian |
|---|---|---|
| stretch | étirement | stretch |
| stretched / linear | étiré / linéaire | con stretch / lineare |
| stretch fit | ajustement par étirement | adattamento dello stretch |
| Linear / Log / Asinh / GHS | Linéaire / Log / Asinh / GHS | Lineare / Log / Asinh / GHS |
| Auto STF / Auto STF (Linked) | Auto STF / Auto STF (lié) | Auto STF / Auto STF (collegato) |
| linked / unlinked | lié / non lié | collegato / scollegato |
| Copy / Paste Stretch | Copier / Coller l'étirement | Copia / Incolla lo stretch |
| normalized / absolute | normalisé / absolu | normalizzato / assoluto |
| histogram | histogramme | istogramma |
| channel | canal | canale |
| common axis | axe commun | asse comune |
| Black / Mid / White | Noir / Médium / Blanc | Nero / Medi / Bianco |
| midtones | tons moyens | mezzitoni |
| black point / white point | point noir / point blanc | punto di nero / punto di bianco |
| ADJUST | AJUSTER | REGOLA |
| adjustments | ajustements | regolazioni |
| Brightness / Contrast / Gamma | Luminosité / Contraste / Gamma | Luminosità / Contrasto / Gamma |
| Shadows / Highlights | Ombres / Hautes lumières | Ombre / Luci |
| Temp / Tint / Hue | Température / Teinte / Rotation | Temperatura / Tinta / Tonalità |
| Saturation / Vibrance | Saturation / Vibrance | Saturazione / Vividezza |
| clip | écrêtage | taglio |
| colormap | palette de couleurs | mappa colori |
| Invert the colormap / ramp | Inverser la palette / rampe | Inverti la mappa colori / rampa |
| break (threshold of the split colormap) | seuil | soglia |
| COLORBAR | BARRE DE COULEURS | BARRA DEI COLORI |
| strength / focus / peak / shape (GHS) | intensité / focalisation / pic / forme | intensità / focalizzazione / picco / forma |
| display function (PixInsight) | fonction d'affichage | funzione di visualizzazione |
| contrast / bias (DS9 scale) | contraste / biais (échelle DS9) | contrasto / bias (scala DS9) |

GHS symbols (D, b, SP, LP, HP) and the R, G, B channel letters stay as they
are. Italian keeps "RGB", so there is no letter change as in French "RVB".

## 5. Image operations

| English | French | Italian |
|---|---|---|
| Rotate 90° CW / CCW | Rotation 90° horaire / antihoraire | Ruota di 90° in senso orario / antiorario |
| Rotate by Angle… | Rotation d'un angle… | Ruota di un angolo… |
| Flip Horizontal / Vertical | Miroir horizontal / vertical | Rifletti orizzontalmente / verticalmente |
| Orient to Sky (North Up, East Left) | Orienter comme le ciel (nord en haut, est à gauche) | Orienta come il cielo (nord in alto, est a sinistra) |
| Orientation at open | Orientation à l'ouverture | Orientamento all'apertura |
| as stored in the file | telle que stockée dans le fichier | come memorizzata nel file |
| Reset Orientation | Réinitialiser l'orientation | Reimposta l'orientamento |
| Crop to Visible Region | Recadrer sur la région visible | Ritaglia sulla regione visibile |
| resampled | rééchantillonné | ricampionata |
| Debayer | Dématriçage | Demosaicizzazione |
| debayered | dématricé | demosaicizzata |
| raw mosaic | mosaïque brute | mosaico grezzo |
| Bilinear / Superpixel (half size) | Bilinéaire / Superpixel (demi-taille) | Bilineare / Superpixel (metà dimensione) |
| Reload Original | Recharger l'original | Ricarica l'originale |
| Auto-Reload Changed Files | Recharger les fichiers modifiés | Ricarica i file modificati |
| Decoding %1… | Décodage de %1… | Decodifica di %1… |
| in-memory image | image en mémoire | immagine in memoria |
| pixel depth, 8-bit / 16-bit per channel | profondeur, 8 / 16 bits par canal | profondità, 8 / 16 bit per canale |

## 6. Image list and culling

| English | French | Italian |
|---|---|---|
| Open Images (the list) | Images ouvertes | Immagini aperte |
| Append files… | Ajouter des fichiers… | Aggiungi file… |
| list entry | entrée de liste | voce dell'elenco |
| selected | sélectionnées | selezionate |
| checked (keep) / unchecked | cochées (à garder) / décochées | contrassegnate (da tenere) / non contrassegnate; in messages "con contrassegno" / "senza contrassegno" |
| Check / Uncheck Selected | Cocher / Décocher la sélection | Contrassegna / Togli il contrassegno alle selezionate |
| Sort: Checked First | Trier : cochées d'abord | Ordina: prima le contrassegnate |
| Move Checked / Unchecked To… | Déplacer les cochées / décochées vers… | Sposta le contrassegnate / non contrassegnate in… |
| Remove … from List | Retirer … de la liste | Rimuovi … dall'elenco |
| Close & Remove from List | Fermer et retirer de la liste | Chiudi e rimuovi dall'elenco |
| Clear List & Close All | Vider la liste et tout fermer | Svuota l'elenco e chiudi tutto |
| Move Frames | Déplacer les images | Sposta le immagini |
| already in the list | déjà dans la liste | già nell'elenco |
| blink | blink | blink |
| image cache | cache d'images | cache delle immagini |

## 7. Astrometry, catalogues and the sky

| English | French | Italian |
|---|---|---|
| RA/Dec | AD/Déc | AR/Dec |
| plate solution / astrometric solution | solution astrométrique | soluzione astrometrica |
| plate scale | échelle | scala dell'immagine |
| rebased (plate solution after a crop) | recalée | riallineata |
| north up, east left | nord en haut, est à gauche | nord in alto, est a sinistra |
| Copy RA/Dec | Copier AD/Déc | Copia AR/Dec |
| Identify in SIMBAD | Identifier dans SIMBAD | Identifica in SIMBAD |
| Look up in Aladin | Consulter dans Aladin | Consulta in Aladin |
| Point Stellarium Here | Pointer Stellarium ici | Punta Stellarium qui |
| Gaia DR3 overlay | superposition Gaia DR3 | sovrapposizione Gaia DR3 |
| source / sources in cone | source / sources dans le cône | sorgente / sorgenti nel cono |
| brightest first | les plus brillantes d'abord | prima le più luminose |
| epoch / sep / plx | époque / sép. / plx | epoca / sep. / plx |
| unreachable, offline? | injoignable, hors ligne ? | non raggiungibile, offline? |
| server / mirror | serveur / miroir | server / mirror |
| SExtractor catalog | catalogue SExtractor | catalogo SExtractor |
| flagged sources | sources marquées | sorgenti segnalate |
| DS9 regions | régions DS9 | regioni DS9 |
| skipped / unsupported / not projectable | ignorées / non prises en charge / non projetables | ignorate / non supportate / non proiettabili |
| sky patch | zone de ciel | zona di cielo |
| Neutralize Background | Neutraliser le fond | Neutralizza il fondo cielo |
| header / keyword / value / comment | en-tête / mot-clé / valeur / commentaire | intestazione / parola chiave / valore / commento |
| HDU, FITS, XISF, WCS | unchanged | unchanged |
| XISF property | propriété XISF | proprietà XISF |
| STRUCTURE / DATA RANGE | STRUCTURE / PLAGE DES DONNÉES | STRUTTURA / INTERVALLO DEI DATI |

## 8. Annotations

| English | French | Italian |
|---|---|---|
| annotation | annotation | annotazione |
| Ellipse / Box / Line / Text | Ellipse / Boîte / Segment / Texte | Ellisse / Riquadro / Linea / Testo |
| label | étiquette | etichetta |
| Annotate Here… | Annoter ici… | Annota qui… |
| anchor point | point d'ancrage | punto di ancoraggio |
| marker radius / line width / text size | rayon du marqueur / épaisseur de trait / taille du texte | raggio del marcatore / spessore della linea / dimensione del testo |
| Clear Annotations | Effacer les annotations | Cancella le annotazioni |
| Invert Annotation Contrast | Inverser le contraste des annotations | Inverti il contrasto delle annotazioni |
| unsaved annotations | annotations non enregistrées | annotazioni non salvate |
| Ignore and Close | Ignorer et fermer | Ignora e chiudi |
| Save Annotations & Display | Enregistrer les annotations et l'affichage | Salva le annotazioni e la visualizzazione |

## 9. Tools: combination, colour transport, PSF

| English | French | Italian |
|---|---|---|
| Combine Channels… | Combiner les canaux… | Combina i canali… |
| preset / role | préréglage / rôle | preimpostazione / ruolo |
| Pre-normalize | Prénormalisation | Prenormalizzazione |
| Luminance / Amount | Luminance / Quantité | Luminanza / Quantità |
| Combine Stars (screen)… | Combiner les étoiles (screen)… | Combina le stelle (screen)… |
| starless image / stars-only image | image sans étoiles / image étoiles seules | immagine senza stelle / immagine solo stelle |
| Transport Colors from Reference… | Transporter les couleurs d'une référence… | Trasporta i colori da un riferimento… |
| reference / strength | référence / force | riferimento / intensità |
| colour match | correspondance de couleurs | corrispondenza dei colori |
| non-destructive | non destructif | non distruttivo |
| Measure PSF (Stars)… | Mesurer la PSF (étoiles)… | Misura la PSF (stelle)… |
| FWHM / eccentricity / PA | FWHM / excentricité / AP | FWHM / eccentricità / AP |
| fitted stars / usable stars | étoiles ajustées / utilisables | stelle modellate / utilizzabili |
| field map | carte de champ | mappa di campo |
| Deconvolve to Target PSF… | Déconvoluer vers une PSF cible… | Deconvolvi verso una PSF obiettivo… |
| kernel | noyau | kernel |
| regularization / noise prior / prior weight | régularisation / a priori de bruit / poids a priori | regolarizzazione / a priori di rumore / peso dell'a priori |
| iterations | itérations | iterazioni |
| round Gaussian | gaussienne circulaire | gaussiana circolare |
| saturated cores (of stars) | cœurs saturés | nuclei stellari saturi |
| feathered | avec fondu | con sfumatura |
| delivered / delivered on proxy | livré / livré par procuration | ottenuta / ottenuta sul sostituto |
| Remove Stars (Analytic)… | Supprimer les étoiles (analytique)… | Rimuovi le stelle (analitico)… |
| Detect above / Fill core above | Détecter au-dessus de / Remplir le cœur au-dessus de | Rileva sopra / Riempi il nucleo sopra |
| noise fill | remplissage de bruit | riempimento con rumore |
| candidates | candidats | candidati |

## 10. Conventions

- **Numbers**: decimal comma, as in "Gamma 0,33–3". Units: "16 bit", "MB",
  "px", "°", "″".
- **Punctuation**: no space before ":" "?" "!" (unlike French). An ellipsis
  "…" closes a menu command that opens a dialog.
- **Capitals**: sentence case in menus, "Salva i dati come…", not title case.
- **Articles**: commands keep the article, "Copia lo stretch", "Combina i
  canali", as Italian macOS applications do.
- **Gender**: "immagine" and "vista" are feminine, so participles agree:
  "ricaricata", "contrassegnate", "orientata come il cielo".
- **Left in English**: blink, stretch, kernel, bias, cache, file, server,
  mirror, superpixel, screen (the blend mode), and all format and catalogue
  names.
- **Undo names** are nouns, because they follow "Annulla": "Annulla
  modifica dello stretch", "Annulla aggiunta di un riquadro".
- **Keyboard names**: "Maiusc" for Shift and "Canc" for Del in messages. Qt
  key strings in the shortcuts file stay as Qt writes them.
- **Counts**: where the code gives a number without a plural form, the
  string carries both endings, "%1 immagine/i".
- **Keyboard accelerators**: each menu gets its own underlined letter,
  chosen so that no two entries of one menu collide.
