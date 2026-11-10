# OXIde

Éditeur / librarian pour l'OSCar (Oxford Synthesiser Company), en plugin AU / VST3 et en appli autonome. Jusqu'à la version 0.4, il s'appelait *OSCar Editor*.
Pensé pour un OSCar équipé de la **carte MIDI et du firmware Tauntek** ; fonctionne aussi en librarian avec le MIDI d'usine.

## Langue

L'interface est en **français** par défaut, avec **English** au choix dans **Réglages**. Les noms des réglages de l'OSCar (Detune, Separation, Glide Type, Trigger…) et de ses touches (INSERT, VOICE, SPACE, TUNE/TRANSPOSE…) restent en anglais, comme sur la façade et dans la doc Tauntek. Les textes sont dans `Resources/Lang/fr.txt` ; un test vérifie que chaque texte de l'interface y a sa traduction.

## Les pages du plugin

- **Voix** : tous les réglages de la voix choisie (potards, faders d'enveloppe, sélecteurs), sur deux étages. En haut le son : molettes, puis oscillateurs → mix → filtre. En bas ce qui le façonne : LFO, glide, triggering, arpégiateur, horloge et les deux enveloppes côte à côte. Des lignes imprimées montrent le trajet du son (en orange) et les modulations (vers ENV AMOUNT, LFO AMOUNT et la hauteur des oscillateurs). Chaque réglage part tout de suite en CC vers l'OSCar (*Live CC*). Tous les paramètres sont aussi exposés au DAW pour l'automation.
- **Générateur** : générateur de sons « intelligent ». *Varier* modifie la voix, de subtil à radical. *Nouveau* crée une basse, un lead, un pad, un son percussif ou une texture à partir des plages et des sections de vraies voix d'OSCar. *Croiser* mélange deux voix. Les sections verrouillées ne bougent pas. Tune, molettes, arpégiateur, Function et Gate Time ne sont jamais modifiés. Des règles garantissent que le résultat s'entend (enveloppe d'ampli, filtre pas fermé, résonance raisonnable…). Chaque résultat joue tout de suite sur l'OSCar sans toucher à sa mémoire, et un historique permet de revenir en arrière.
- **Morphing** : deux voix A et B et un crossfader façon Octatrack. L'interrupteur **MORPH** est éteint au démarrage : le fader et son automation ne font alors rien. Allumé, l'OSCar joue le mélange : les potards glissent de A vers B, les sélecteurs basculent à mi-course. C'est non destructif : aucune voix ne change, le mélange part seulement vers l'OSCar en CC (Live CC). *Garder le mélange* l'écrit dans la voix choisie (une étape d'annulation). Chaque côté a un seul afficheur : un clic ouvre la liste (la voix choisie en tête), et la voix prise est chargée. Le *lissage* fixe le temps le plus court pour aller de A à B : un geste brusque devient un glissement ; allumer MORPH ou charger une voix glisse aussi. Le plugin n'envoie que les CC qui ont changé, au plus 12 toutes les 33 ms (les plus gros mouvements d'abord), soit environ un tiers d'un câble MIDI, et rien pendant un transfert. Éteindre MORPH, choisir une autre voix ou en générer une rend la voix choisie à l'OSCar. Le fader est automatisable dans le DAW (paramètre *Morph A-B*).
- **Banques** : la banque avec ses noms (renommer, copier, coller, échanger, Init pour partir de zéro, revenir à la version de l'OSCar, enregistrer / importer une voix), et la bibliothèque. Celle-ci rassemble tes banques, tes voix et toutes les sauvegardes automatiques ; un double-clic en charge une.
- **Formes d'onde** : les 5 formes d'onde utilisateur (−8 à −12), en synthèse additive : 24 bandes, une par harmonique, à dessiner à la souris. Un trait à travers les bandes les règle toutes au passage, même rapide. Il faut d'abord les recevoir de l'OSCar (INSERT + une touche WAVEFORM + SPACE), puis *Envoyer les formes d'onde*.
- **Raccourcis Tauntek** : le clavier de l'OSCar avec la fonction de chaque raccourci TUNE/TRANSPOSE + touche. Les touches qui remplacent les voix en mémoire sont en rouge.
- **Réglages** : ports MIDI, canal, langue, options (Live CC, horloge MIDI, MIDI Thru, suivre l'OSCar), état du patch CC dump, mises à jour et journal.

## Rappel automatique (« total recall »)

Quand tu choisis une voix sur l'OSCar, la carte Tauntek envoie son *patch CC dump*. Le plugin reconstruit alors le son que joue l'OSCar et met l'affichage à jour : sélecteurs exacts (Function Normal / Arp / Duo, Trigger, arpégiateur… sont enregistrés dans chaque voix), potards à un cran près (le dump est en 7 bits). Annuler permet de revenir en arrière. Exception : si la voix a des modifications pas encore envoyées dans le plugin, c'est la version du plugin qui part vers l'OSCar. Le voyant **CC DUMP** indique si l'OSCar envoie bien ces dumps.

## Horloge MIDI et arpégiateur

Sur la page **Voix**, la colonne **CLOCK** :

- **Sync** (sélecteur INT / MIDI) : *Interne*, l'arpégiateur et le séquenceur suivent le **Tempo** (afficheur orange : glisser vers le haut ou le bas sur le nombre, ou molette) ; *Horloge MIDI*, ils avancent sur l'horloge MIDI que reçoit l'OSCar. C'est le réglage **Trigger = Ext** de l'OSCar, enregistré dans la voix. Revenir en *Interne* remet le Trigger d'avant.
- **Division** : la valeur d'un pas quand la voix suit l'horloge : 1/96, 1/48, 1/32, 1/16T, 1/16, 1/8T, 1/8 ou 1/4T (T = triolet). Le firmware Tauntek la tire du Tempo global (Tempo ÷ 32 donne 1, 2, 3, 4, 6, 8, 12 ou 16 impulsions d'horloge par pas). Le plugin envoie donc la bonne valeur de Tempo (CC 54, avec Live CC), et l'afficheur Tempo montre la division.
- En *Interne*, un Tempo en dessous de 32 fait aussi avancer l'arpégiateur à chaque impulsion d'horloge MIDI (comportement du firmware).

L'horloge peut venir de n'importe quel appareil branché sur l'OSCar, ou du DAW : l'option **Envoyer l'horloge MIDI du DAW à l'OSCar** (page **Réglages**, désactivée par défaut) envoie 24 impulsions par noire au tempo du DAW, avec Start (depuis le début), Continue (en cours de morceau) et Stop. Les impulsions sont placées à leur position exacte dans chaque bloc audio. Rien ne part pendant un export hors ligne ni pendant un transfert de banque. À laisser désactivé si le DAW envoie déjà l'horloge sur le port de l'OSCar.

## Random et Scale

En haut de la fenêtre, sur toutes les pages : **RANDOM** à gauche du logo, **SCALE** à droite, comme dans MIFM1.

- **RANDOM** allumé : tourner un potard ou une tirette de la page Voix règle combien il suit SHAPE au lieu de sa valeur (aussi Transpose, Osc 2 Interval et Octave Shift). Plus loin, plus large (au bout : toute la course de part et d'autre). Au milieu, ou double-clic : rien, la commande retrouve sa valeur ; y revenir tire une autre boucle pour la fois suivante. RANDOM à nouveau : les potards se tournent normalement, et une petite LED marque ceux qui bougent (rouge : vers le haut, ambre : vers le bas). Clic droit sur RANDOM : le tempo sans DAW (application seule ; dans un DAW, c'est le sien), nouvelles boucles, tout effacer.
- **SHAPE** : *S&H*, une nouvelle valeur à chaque pas (vers le haut) ou des valeurs qui reviennent les mêmes à chaque boucle (vers le bas) ; *SMOOTH*, pareil en glissant d'une valeur à la suivante ; ou une des ondes des LFO de MIFM1 (SINE, TRIANGLE, SAW, RAMP, SQUARE, PUMP, STAIRS, BOUNCE, CLAVE, RISES, FALLS, ZIGZAG) : un seul LFO que suivent toutes les commandes, à l'envers vers le bas. Les ondes et les glissements partent au fil des valeurs, 400 CC par seconde au plus (environ un tiers d'un câble MIDI), les plus gros mouvements d'abord.
- **RATE** : un pas (S&H, SMOOTH) ou un cycle d'onde tous les 1/32 à 16 mesures, sur le tempo du DAW ; quand il joue, les pas, les boucles et les ondes suivent ses mesures. **LOOP** : la longueur des boucles, de 2 à 64 pas.
- La voix ne change pas : l'OSCar reçoit les valeurs en CC (Live CC) et retrouve celles de la voix quand l'aléatoire s'arrête (retour au milieu, Live CC coupé). Les aléatoires et leurs boucles sont enregistrés avec le projet.
- **SCALE** et **ROOT** : les gammes de MIFM1 (majeur, mineur, dorien, phrygien, lydien, mixolydien, locrien, mineur harmonique et mélodique, pentatoniques majeure et mineure, blues, par tons, hirajoshi, phrygien dominant). Transpose et Osc 2 Interval ne prennent que les notes de la gamme (comptées depuis ROOT ; Osc 2 depuis là où Transpose met l'oscillateur 1), leurs aléatoires aussi. Avec MIDI Thru, les notes jouées vont à la note de la gamme la plus proche, et les aléatoires comptent depuis la dernière note jouée. Frequency (le filtre) n'a pas de correspondance en notes : il reste libre.

## Rappel du son à l'ouverture du projet

Option dans Réglages, activée par défaut : *Rappeler le son à l'ouverture du projet*. Le plugin enregistre avec le projet ses 36 voix et la voix choisie. À l'ouverture, l'OSCar reprend cette voix exactement comme dans le projet, quoi que contiennent ses mémoires : toutes ses valeurs en CC (sans changement de programme, donc sans charger ni comparer sa mémoire), le tempo, et les formes d'onde utilisateur (−8 à −12, en SysEx) si la voix en joue une. Rien n'est écrit dans les mémoires de voix de l'OSCar. Si la sortie MIDI n'est pas là dans les 10 secondes, un message le dit.

## MIDI Thru

Option (désactivée par défaut) pour jouer l'OSCar depuis la piste du DAW, donc avec un autre clavier. Seuls les notes (avec vélocité) et le pitch bend passent, sur le canal de l'OSCar, à leur position dans le bloc audio. Les autres CC sont filtrés pour ne pas changer le son derrière le dos du plugin. Rien ne passe pendant un transfert de banque, et les notes relâchées pendant ce temps sont coupées à la fin. Une note jouée sur le clavier de l'OSCar qui revient par le DAW (monitoring de piste) n'est pas renvoyée. La molette de modulation (CC 1) ne passe pas : le firmware Tauntek ne la reçoit pas.

## Sauvegardes

Tout ce qui est reçu et envoyé est sauvegardé dans `Documents/OXIde/Backups`. Avant chaque envoi, le contenu connu de l'OSCar est aussi sauvegardé. *Enregistrer la banque…* et *Exporter en .syx…* demandent juste un nom et enregistrent directement dans `Documents/OXIde/Banks` ; *Sauver voix…* enregistre la voix sélectionnée dans `Documents/OXIde/Voices`. Rien n'est jamais écrasé (un numéro est ajouté si le nom existe déjà). Le dossier `Documents/OSCar Editor` des versions 0.4 et avant est renommé une fois, avec tout son contenu. Tout se recharge depuis la bibliothèque de la page **Banques** (filtres *Mes banques*, *Mes voix*, *Sauvegardes*) : un double-clic sur une banque remplace la banque de l'éditeur, sur une voix la met dans la voix sélectionnée.

## Mises à jour

Le plugin vérifie au plus une fois par jour les releases publiques de `distortedimages-code/OSCarEditor` (réglable avec `-DOSCAR_UPDATE_REPO=propriétaire/dépôt`). Quand une version plus récente existe, un bouton **Mise à jour** apparaît. Il télécharge la release, vérifie sa somme SHA-256, puis remplace l'AU, le VST3 et l'appli installés sur ce Mac. Une copie encore nommée *OSCar Editor* est remplacée par *OXIde* au même endroit ; si les deux noms se côtoient, le plugin propose au démarrage de mettre l'ancien à la corbeille. Il faut ensuite quitter et relancer le DAW. Comme c'est le plugin qui télécharge, macOS ne met pas le fichier en quarantaine. Seuls `api.github.com` et le serveur de téléchargement de GitHub sont contactés. Sous Windows, le bouton ouvre la page de téléchargement.

**Publier une version** :
1. Changer `VERSION` dans `CMakeLists.txt` (par ex. `0.5.0`) et écrire les nouveautés dans `RELEASE_NOTES.md`, puis committer sur `main`.
2. Sur GitHub : onglet *Actions* → **release** → **Run workflow** (branche `main`). Autre possibilité, en ligne de commande : `git tag v0.5.0 && git push origin v0.5.0`.
3. GitHub compile, teste et crée un **brouillon** de release `v0.5.0` avec `OXIde-mac.dmg`, `OXIde-mac.zip` et sa somme `.sha256`, plus `OSCar-Editor-mac.zip` et sa somme : le même plugin sous les anciens noms de bundles, pour le système de mise à jour des versions 0.4 et avant (une dizaine de minutes).
4. Sur la page *Releases*, relire les notes puis **Publish release** (le tag est créé à ce moment-là). Les plugins installés la proposent au plus tard le lendemain (ou tout de suite avec *Vérifier*, page **Réglages**).

## Site web

La page de présentation (téléchargement, nouveautés, guide d'installation, astuces, retours) est servie par GitHub Pages depuis le dossier `docs/` : https://distortedimages-code.github.io/OSCarEditor/. Elle lit elle-même les releases publiées sur GitHub, donc une nouvelle version y apparaît sans rien modifier. Pour changer ses textes : modifier `tools/site/template.html`, puis lancer `python3 tools/site/make_site.py`.

## Vérifié sur le firmware Tauntek

Les données du plugin ont été recoupées avec le code du firmware Tauntek (`tntkoscr.bin`, somme de contrôle 9D73 conforme au readme) et avec le tableau des CC :

- **Ordre des octets d'une voix** : les routines CC du firmware écrivent le tampon de la voix exactement dans l'ordre du format SysEx utilisé par le plugin (26 potards, puis 12 sélecteurs).
- **Valeurs des sélecteurs** : tirées des tables du firmware. Glide Type, Filter Type et Function valent 0, 1, 2, 4, 5, 6 (le bit 2 est un drapeau), pas 0 à 5 comme l'indiquent le PDF et la doc communautaire.
- **Réglages Tauntek** : rangés dans le bit 3 des valeurs Function (LFO > Osc 1), LFO Wave (LFO Sync), Filter Type et Trigger (les deux bits de PWM Rate).
- **Encodage CC et patch dump** : chaque valeur envoyée, passée dans les tables du firmware, retombe exactement sur la valeur voulue ; le patch dump est décodé avec la routine du firmware (0x59F2).
- **Particularité gérée** : le CC 51 (arpégiateur) modifie aussi le bit 2 de Function. Le plugin renvoie Function juste après.
- **MIDI reçu par la carte** : notes, CC, program change et pitch bend (dispatch du firmware à 0x1D0F).
- **Horloge MIDI** : F8 / Start / Continue / Stop (routine 0x1C6B) ; avec Trigger = Ext, un pas toutes les 1, 2, 3, 4, 6, 8, 12 ou 16 impulsions selon Tempo ÷ 32 (routine 0x0A35).

Le firmware lui-même n'est pas inclus dans le projet ; seules les tables utiles sont reprises, dans les tests.

Ce qui reste **à confirmer à l'oreille** : les *noms* de certains sélecteurs (marqués « ? »). Les valeurs, elles, sont justes.

## Installation sur Mac

**Depuis une release** : ouvrir `OXIde-mac.dmg` et glisser chaque fichier sur le raccourci de son dossier (Components, VST3, Applications), puis débloquer le plugin une fois avec le texte à coller dans Terminal donné sur le site (il retire les copies à l'ancien nom, la quarantaine, et fait valider l'AU par `auval`, comme Logic Pro). L'image disque est construite par `tools/dmg/make_dmg.sh` (dmgbuild) ; son fond se refait avec `tools/dmg/make_background.py`. Le logo (le même que dans le plugin) est généré en SVG par `tools/logo/make_logo.py`.

**Depuis les sources** :

1. Ouvrir le Terminal dans ce dossier.
2. `./build_mac.sh`

Le script vérifie les outils, compile, **lance les tests et n'installe rien si un test échoue**, puis installe l'AU, le VST3 et l'appli pour ton utilisateur. Les tests ne touchent ni tes ports MIDI ni tes dossiers.

Sans outils de dev : pousser ce dossier sur GitHub, l'onglet *Actions* compile Mac et Windows et fournit les fichiers à télécharger.

## Réglages sur l'OSCar

- **Program change** : maintenir SPACE, appuyer sur la touche −9 (LED −9 allumée). Indispensable pour que le plugin et l'OSCar jouent la même voix.
- **Patch CC dump** : maintenir TUNE/TRANSPOSE, appuyer sur la touche « Enable patch CC dump » (page *Raccourcis Tauntek* du plugin). Indispensable pour le rappel automatique.
- Si l'OSCar ne suit plus après avoir été éteint, refaire ces deux réglages.
- **Canal MIDI** : maintenir SPACE et appuyer sur une touche de 1 à 16 le change (la carte Tauntek le garde après extinction). Hors omni, l'OSCar n'entend que ce canal : il doit être celui de la page Réglages. Si l'OSCar émet sur un autre canal, l'afficheur du plugin indique « OSCAR CANAL » et son numéro, et le bouton *Passer sur le canal N* met le plugin sur celui de l'OSCar (le canal de l'OSCar lui-même ne se change que sur l'OSCar : son firmware n'accepte aucun message MIDI pour ça). En maintenant SPACE, les LED montrent les options MIDI actives (2e : program change, 3e : notes, 5e : omni).
- Attention aux touches « Load factory / B / C patches » et « Load patches from save area » : elles remplacent les 36 voix en mémoire.

**Dump depuis l'OSCar** : maintenir INSERT, appuyer sur VOICE (ou SEQ, un bouton WAVEFORM, ou STORE pour tout), puis SPACE.

## Branchements

- OSCar MIDI OUT → entrée MIDI de l'interface ; sortie MIDI de l'interface → OSCar MIDI IN. Choisir les deux ports dans la page **Réglages**.
- Le plugin ouvre les ports lui-même, parce que dans Logic Pro (comme dans la plupart des DAW) un instrument AU ne peut pas envoyer de MIDI vers une interface. Pour jouer l'OSCar depuis le DAW : soit *MIDI Thru* dans le plugin, soit un External Instrument sur une autre piste (pas les deux à la fois).
- Sous Windows, un port MIDI ne s'ouvre que dans une seule appli à la fois.

## Ce que garantissent les tests (lancés à chaque compilation)

- **Codec SysEx** : décodage puis réencodage identique à l'octet près sur la banque d'usine. Modifier un paramètre ne touche que ses propres bits. Tout fichier ou dump abîmé est refusé sans rien modifier.
- **CC** : numéros conformes au tableau Tauntek ; décodage conforme aux tables du firmware pour les 128 valeurs.
- **Rappel** : pour chaque couple de voix d'usine et 20 000 voix aléatoires, la voix reconstruite depuis le patch dump a ses sélecteurs exacts et ses potards à un cran près.
- **Générateur** : des milliers de générations par mode et par famille donnent toujours des voix valides. Les verrous et les réglages protégés sont respectés, aucune règle d'audibilité n'est violée, et le résultat est le même pour une même graine. Les familles se distinguent bien (attaque des pads, sustain des sons percussifs, coupure des basses).
- **De bout en bout**, via un port virtuel : rien n'est envoyé au démarrage ni au chargement d'un projet ; une édition donne un seul CC ; le rappel depuis l'OSCar ne renvoie rien ; les modifications non envoyées gagnent ; le MIDI Thru filtre, remappe le canal, ignore les échos et ne laisse aucune note bloquée ; l'horloge MIDI donne Start / Continue, 24 impulsions par noire régulièrement espacées, puis Stop, et rien en export hors ligne ; l'automation n'est pas rejouée ; les CC attendent la fin d'un transfert ; le morphing glisse, bascule les sélecteurs à mi-course, espace ses CC et s'annule en une fois ; l'interface s'ouvre sur chaque page et à plusieurs tailles.
- **Traduction** : chaque texte de l'interface a sa traduction avec les mêmes paramètres (%1…), le fichier se charge en entier, retours à la ligne compris.
- **Mises à jour** : comparaison des versions, lecture de la réponse de GitHub, somme de contrôle, remplacement des bundles. Une version incorrecte, un fichier illisible ou un format absent de la release laissent l'installation intacte.
- Aucune course de données sous ThreadSanitizer, aucune assertion JUCE en mode debug.

## Sources

- Firmware et tableau CC Tauntek (Bob Grieb, 2021).
- Manuel MIDI de l'OSCar (Kenton) : procédures, ordre des voix, program change.
- [codemechanic/oscar-sysex-grammar](https://github.com/codemechanic/oscar-sysex-grammar) (MIT) : structure SysEx et banque d'usine.
- Polices : Barlow Semi Condensed, Cormorant SC, Share Tech Mono (SIL Open Font License, voir `Resources/Fonts`).

## Arborescence

- `Source/core/` : codec SysEx, carte des paramètres, encodage CC, rappel depuis le patch dump, générateur. Sans JUCE.
- `Source/` : plugin JUCE (modèle, ports MIDI, moteur CC, MIDI Thru, mises à jour).
- `Source/ui/` : style « Atelier » (bois, potards, faders, voyants…), les sept pages et la langue.
- `Resources/Lang/fr.txt` : traduction française.
- `Tests/` : `CoreTests`, `GeneratorTests`, `DocTests`, `ProcessorTests`, `UpdaterTests`, `check_translations.py`.
- `.github/workflows/` : `build.yml` (compilation à chaque push), `release.yml` (release sur tag `v*`).
