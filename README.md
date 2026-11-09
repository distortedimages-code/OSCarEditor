# OSCar Editor

Éditeur / librarian pour l'OSCar (Oxford Synthesiser Company), en plugin AU / VST3 et en appli autonome.
Pensé pour un OSCar équipé de la **carte MIDI et du firmware Tauntek** ; fonctionne aussi en librarian avec le MIDI d'usine.

## Langue

L'interface est en **français** par défaut, avec **English** au choix dans **Réglages**. Les noms des réglages de l'OSCar (Detune, Separation, Glide Type, Trigger…) et de ses touches (INSERT, VOICE, SPACE, TUNE/TRANSPOSE…) restent en anglais, comme sur la façade et dans la doc Tauntek. Les textes sont dans `Resources/Lang/fr.txt` ; un test vérifie que chaque texte de l'interface y a sa traduction.

## Les pages du plugin

- **Voix** : tous les réglages de la voix choisie (potards, faders d'enveloppe, sélecteurs). Chaque réglage part tout de suite en CC vers l'OSCar (*Live CC*). Tous les paramètres sont aussi exposés au DAW pour l'automation.
- **Générateur** : générateur de sons « intelligent ». *Varier* modifie la voix, de subtil à radical. *Nouveau* crée une basse, un lead, un pad, un son percussif ou une texture à partir des plages et des sections de vraies voix d'OSCar. *Croiser* mélange deux voix. Les sections verrouillées ne bougent pas. Tune, molettes, arpégiateur, Function et Gate Time ne sont jamais modifiés. Des règles garantissent que le résultat s'entend (enveloppe d'ampli, filtre pas fermé, résonance raisonnable…). Chaque résultat joue tout de suite sur l'OSCar sans toucher à sa mémoire, et un historique permet de revenir en arrière.
- **Banques** : la banque avec ses noms (renommer, copier, coller, échanger, revenir à la version de l'OSCar, enregistrer / importer une voix), et la bibliothèque. Celle-ci rassemble tes banques, tes voix et toutes les sauvegardes automatiques ; un double-clic en charge une.
- **Formes d'onde** : les 5 formes d'onde utilisateur (−8 à −12), à dessiner à la souris.
- **Raccourcis Tauntek** : le clavier de l'OSCar avec la fonction de chaque raccourci TUNE/TRANSPOSE + touche. Les touches qui remplacent les voix en mémoire sont en rouge.
- **Réglages** : ports MIDI, canal, langue, options (Live CC, horloge MIDI, MIDI Thru, suivre l'OSCar), état du patch CC dump, mises à jour et journal.

## Rappel automatique (« total recall »)

Quand tu choisis une voix sur l'OSCar, la carte Tauntek envoie son *patch CC dump*. Le plugin reconstruit alors le son que joue l'OSCar et met l'affichage à jour : sélecteurs exacts (Function Normal / Arp / Duo, Trigger, arpégiateur… sont enregistrés dans chaque voix), potards à un cran près (le dump est en 7 bits). Annuler permet de revenir en arrière. Exception : si la voix a des modifications pas encore envoyées dans le plugin, c'est la version du plugin qui part vers l'OSCar. Le voyant **CC DUMP** indique si l'OSCar envoie bien ces dumps.

## Horloge MIDI et arpégiateur

Sur la page **Voix**, le bloc **horloge midi** :

- **Sync** : *Interne*, l'arpégiateur et le séquenceur suivent le potard **Tempo** ; *Horloge MIDI*, ils avancent sur l'horloge MIDI que reçoit l'OSCar. C'est le réglage **Trigger = Ext** de l'OSCar, enregistré dans la voix. Revenir en *Interne* remet le Trigger d'avant.
- **Division** : la valeur d'un pas quand la voix suit l'horloge : 1/96, 1/48, 1/32, 1/16T, 1/16, 1/8T, 1/8 ou 1/4T (T = triolet). Le firmware Tauntek la tire du Tempo global (Tempo ÷ 32 donne 1, 2, 3, 4, 6, 8, 12 ou 16 impulsions d'horloge par pas). Le plugin envoie donc la bonne valeur de Tempo (CC 54, avec Live CC), et le potard Tempo affiche la division.
- En *Interne*, un Tempo en dessous de 32 fait aussi avancer l'arpégiateur à chaque impulsion d'horloge MIDI (comportement du firmware).

L'horloge peut venir de n'importe quel appareil branché sur l'OSCar, ou du DAW : l'option **Envoyer l'horloge MIDI du DAW à l'OSCar** (page **Réglages**, désactivée par défaut) envoie 24 impulsions par noire au tempo du DAW, avec Start (depuis le début), Continue (en cours de morceau) et Stop. Les impulsions sont placées à leur position exacte dans chaque bloc audio. Rien ne part pendant un export hors ligne ni pendant un transfert de banque. À laisser désactivé si le DAW envoie déjà l'horloge sur le port de l'OSCar.

## MIDI Thru

Option (désactivée par défaut) pour jouer l'OSCar depuis la piste du DAW, donc avec un autre clavier. Seuls les notes (avec vélocité) et le pitch bend passent, sur le canal de l'OSCar, à leur position dans le bloc audio. Les autres CC sont filtrés pour ne pas changer le son derrière le dos du plugin. Rien ne passe pendant un transfert de banque, et les notes relâchées pendant ce temps sont coupées à la fin. Une note jouée sur le clavier de l'OSCar qui revient par le DAW (monitoring de piste) n'est pas renvoyée. La molette de modulation (CC 1) ne passe pas : le firmware Tauntek ne la reçoit pas.

## Sauvegardes

Tout ce qui est reçu et envoyé est sauvegardé dans `Documents/OSCar Editor/Backups`. Avant chaque envoi, le contenu connu de l'OSCar est aussi sauvegardé. *Enregistrer la banque…* et *Exporter en .syx…* demandent juste un nom et enregistrent directement dans `Documents/OSCar Editor/Banks` ; *Sauver voix…* enregistre la voix sélectionnée dans `Documents/OSCar Editor/Voices`. Rien n'est jamais écrasé (un numéro est ajouté si le nom existe déjà). Tout se recharge depuis la bibliothèque de la page **Banques** (filtres *Mes banques*, *Mes voix*, *Sauvegardes*) : un double-clic sur une banque remplace la banque de l'éditeur, sur une voix la met dans la voix sélectionnée.

## Mises à jour

Le plugin vérifie au plus une fois par jour les releases publiques de `distortedimages-code/OSCarEditor` (réglable avec `-DOSCAR_UPDATE_REPO=propriétaire/dépôt`). Quand une version plus récente existe, un bouton **Mise à jour** apparaît. Il télécharge la release, vérifie sa somme SHA-256, puis remplace l'AU, le VST3 et l'appli installés sur ce Mac. Il faut ensuite quitter et relancer le DAW. Comme c'est le plugin qui télécharge, macOS ne met pas le fichier en quarantaine. Seuls `api.github.com` et le serveur de téléchargement de GitHub sont contactés. Sous Windows, le bouton ouvre la page de téléchargement.

**Publier une version** :
1. Changer `VERSION` dans `CMakeLists.txt` (par ex. `0.3.0`) et écrire les nouveautés dans `RELEASE_NOTES.md`, puis committer sur `main`.
2. Sur GitHub : onglet *Actions* → **release** → **Run workflow** (branche `main`). Autre possibilité, en ligne de commande : `git tag v0.3.0 && git push origin v0.3.0`.
3. GitHub compile, teste et crée un **brouillon** de release `v0.3.0` avec `OSCar-Editor-mac.zip` et sa somme `.sha256` (une dizaine de minutes).
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

**Depuis une release** : ouvrir `OSCar-Editor-mac.dmg`, glisser chaque fichier sur le raccourci de son dossier (Components, VST3, Applications), puis dans Terminal taper `bash `, glisser `Installer.command` et appuyer sur Entrée. Le script installe ce qui manque (dans `~/Library` si aucune copie n'est dans `/Library`), retire la quarantaine, signale les doublons et fait valider l'AU par `auval`, comme Logic Pro. L'image disque est construite par `tools/dmg/make_dmg.sh` (dmgbuild) ; le fond de fenêtre se refait avec `tools/dmg/make_background.py`.

**Depuis les sources** :

1. Ouvrir le Terminal dans ce dossier.
2. `./build_mac.sh`

Le script vérifie les outils, compile, **lance les tests et n'installe rien si un test échoue**, puis installe l'AU, le VST3 et l'appli pour ton utilisateur. Les tests ne touchent ni tes ports MIDI ni tes dossiers.

Sans outils de dev : pousser ce dossier sur GitHub, l'onglet *Actions* compile Mac et Windows et fournit les fichiers à télécharger.

## Réglages sur l'OSCar

- **Program change** : maintenir SPACE, appuyer sur la touche −9 (LED −9 allumée). Indispensable pour que le plugin et l'OSCar jouent la même voix.
- **Patch CC dump** : maintenir TUNE/TRANSPOSE, appuyer sur la touche « Enable patch CC dump » (page *Raccourcis Tauntek* du plugin). Indispensable pour le rappel automatique.
- Si l'OSCar ne suit plus après avoir été éteint, refaire ces deux réglages.
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
- **De bout en bout**, via un port virtuel : rien n'est envoyé au démarrage ni au chargement d'un projet ; une édition donne un seul CC ; le rappel depuis l'OSCar ne renvoie rien ; les modifications non envoyées gagnent ; le MIDI Thru filtre, remappe le canal, ignore les échos et ne laisse aucune note bloquée ; l'horloge MIDI donne Start / Continue, 24 impulsions par noire régulièrement espacées, puis Stop, et rien en export hors ligne ; l'automation n'est pas rejouée ; les CC attendent la fin d'un transfert ; l'interface s'ouvre sur chaque page et à plusieurs tailles.
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
- `Source/ui/` : style « Atelier » (bois, potards, faders, voyants…), les six pages et la langue.
- `Resources/Lang/fr.txt` : traduction française.
- `Tests/` : `CoreTests`, `GeneratorTests`, `DocTests`, `ProcessorTests`, `UpdaterTests`, `check_translations.py`.
- `.github/workflows/` : `build.yml` (compilation à chaque push), `release.yml` (release sur tag `v*`).
