# 🐍 Snake — Matrice LED avec contrôle par accéléromètre

Projet réalisé dans le cadre du cours Arduino — Ingénierie des Médias (HEIG-VD)

---

## 📋 Description

Implémentation du jeu Snake classique sur une matrice de LEDs WS2812 8×8, contrôlée par les mouvements physiques de la carte grâce à un accéléromètre/gyroscope ICM-20948. Le joueur incline la carte pour diriger le serpent.

### Fonctionnalités
- Déplacement du serpent via inclinaison (accéléromètre ICM-20948)
- Matrices LED 8×8 WS2812 (NeoPixel)
- Système de 3 vies avec LEDs indicatrices
- Accélération progressive au fil des pommes mangées
- Buzzer pour les effets sonores (manger, perdre une vie, game over)
- Bouton restart
- Calibration automatique de l'IMU au démarrage

---

## 🛒 Matériel nécessaire


Arduino (Uno / Nano / compatible) ,
Matrices LED WS2812 8×8 ,
IMU ICM-20948 (breakout I2C) ,
Buzzer passif ,
LEDs (vies) ,
Résistances 220Ω,
Bouton poussoir ,
Câbles Dupont ,

---

## 🔌 Schéma de câblage

```
Arduino          Matrice WS2812 8x8
  5V      ────►  5V
  GND     ────►  GND
  D6      ────►  DIN

Arduino          ICM-20948 (I2C)
  3.3V    ────►  VCC
  GND     ────►  GND
  A4/SDA  ────►  SDA
  A5/SCL  ────►  SCL
  3.3V    ────►  AD0   (→ adresse 0x69)

Arduino          Buzzer
  D9      ────►  +
  GND     ────►  -

Arduino          LEDs vies (avec résistance 220Ω en série)
  D3      ────►  LED Vie 1
  D4      ────►  LED Vie 2
  D5      ────►  LED Vie 3
  GND     ────►  cathode commune

Arduino          Bouton restart
  D2      ────►  broche 1
  GND     ────►  broche 2  (INPUT_PULLUP)
```

> ⚠️ **Important** : L'AD0 du ICM-20948 est connecté au 3.3V → adresse I2C `0x69`. Si AD0 est relié à GND, l'adresse est `0x68` et il faut changer `AD0_VAL = 0` dans le code.

---

## 📦 Dépendances (bibliothèques Arduino)

À installer via le **Gestionnaire de bibliothèques** (Croquis → Inclure une bibliothèque) :

| Bibliothèque | Auteur |
|---|---|
| `Adafruit NeoPixel` | Adafruit |
| `ICM 20948` | SparkFun |

---

## 🚀 Installation et lancement

1. Cloner le dépôt :
   ```bash
   git clone https://github.com/ton-user/snake-arduino.git
   ```
2. Ouvrir `snake.ino` dans l'IDE Arduino
3. Installer les bibliothèques listées ci-dessus
4. **Poser la carte à plat et immobile** avant de brancher (calibration IMU au démarrage)
5. Téléverser le code
6. Attendre ~2 secondes que la calibration se termine → le jeu démarre automatiquement

---

## 🎮 Comment jouer

| Action | Commande |
|---|---|
| Diriger le serpent | Incliner la carte (haut / bas / gauche / droite) |
| Redémarrer | Appuyer sur le bouton (D2) |

- Manger une pomme (vert) fait grandir le serpent et accélère le jeu
- Toucher un mur ou soi-même fait perdre une vie
- 3 vies disponibles — game over affiché par une croix rouge sur la matrice

---

## 🧠 Explications du code

### Lecture de l'accéléromètre et calcul des angles

L'IMU retourne les accélérations brutes sur 3 axes (en mg). Ces valeurs sont d'abord lissées avec un **filtre passe-bas (LPF)** pour éliminer le bruit :

```cpp
fax = (1 - LPF_WEIGHT) * fax + LPF_WEIGHT * ax;
```

Les angles **roll** et **pitch** sont ensuite calculés via `atan2` :

```cpp
rollDeg  = atan2f(fay, faz) * 57.2958f;
pitchDeg = atan2f(-fax, sqrtf(fay*fay + faz*faz)) * 57.2958f;
```

### Calibration au démarrage

Au démarrage, 50 lectures sont effectuées pour **chauffer le filtre**, puis 100 lectures sont moyennées pour établir les offsets `roll0` et `pitch0` (position de repos). Toutes les directions sont calculées en **delta** par rapport à ces offsets.

### Détection de direction

Les deltas `dr` (roll) et `dp` (pitch) sont normalisés entre -180° et +180° pour éviter les sauts autour de ±180° :

```cpp
if (dr > 180.0f) dr -= 360.0f;
```

Une direction n'est déclenchée que si :
- L'angle dépasse `TRIG_DEG` (seuil de déclenchement)
- L'axe est **dominant** par rapport à l'autre (évite les diagonales)
- Le cooldown `DIR_COOLDOWN_MS` est écoulé (anti-rebond)

### Logique du jeu

Le serpent est stocké dans un tableau `snake[]` de `Point`. À chaque tick :
1. La tête avance dans la direction courante
2. Si collision mur ou soi-même → perte de vie
3. Si pomme mangée → `snakeLen++`, nouvelle pomme, accélération
4. Le corps est décalé : `snake[i] = snake[i-1]`

### Mapping des LEDs

La matrice est câblée ligne par ligne (non serpentin) :

```cpp
uint16_t xyToIndex(uint8_t x, uint8_t y) {
  return y * W + x;
}
```

---

## ⚙️ Paramètres configurables

| Paramètre | Valeur | Rôle |
|---|---|---|
| `LPF_WEIGHT` | 0.25 | Réactivité du filtre (0.1 = lent, 0.3 = rapide) |
| `DEAD_DEG` | 8.0° | Zone neutre (évite les faux positifs) |
| `TRIG_DEG` | 18.0° | Angle minimum pour changer de direction |
| `DOMINANCE` | 1.20 | Ratio dominant/secondaire pour éviter les diagonales |
| `DIR_COOLDOWN_MS` | 80ms | Délai minimum entre deux changements de direction |
| `START_STEP_MS` | 250ms | Vitesse initiale du serpent |
| `MIN_STEP_MS` | 90ms | Vitesse maximale |

---

## 👤 Auteur

Projet réalisé par **[Léa Bugnon]** — HEIG-VD, cours Arduino, 2025
