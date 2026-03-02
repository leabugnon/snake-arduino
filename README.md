# 🐍 Snake Arduino — Contrôle par accéléromètre (ICM-20948)

Projet réalisé dans le cadre du cours Arduino — Ingénierie des Médias (HEIG-VD)

---

## 📋 Description

Ce projet consiste en l’implémentation du jeu **Snake** sur un affichage LED adressable, contrôlé par l’inclinaison physique de la carte Arduino à l’aide d’un capteur IMU **ICM-20948** (accéléromètre + gyroscope).

Le système a été conçu de manière **modulaire et évolutive** afin de pouvoir fonctionner sur un ou plusieurs panneaux LED (ex. 1 panneau 8×8 pour les tests, puis 4 panneaux assemblés pour une surface d’affichage plus grande).

---

## 🧪 Évolution du projet

### 🎮 V1 — Contrôle par joystick
- Déplacement du serpent avec un joystick analogique
- Première implémentation du moteur du jeu Snake
- Validation de la logique de déplacement et des collisions

### 🧭 V2 — Contrôle par accéléromètre (première implémentation)
- Remplacement du joystick par l’IMU ICM-20948
- Lecture brute de l’accéléromètre
- Apparition de bruit et de contrôles peu intuitifs
- Premiers tests de pitch/roll

### 🚀 V3 — Accéléromètre optimisé (version actuelle)
- Filtre passe-bas sur les axes X, Y, Z
- Calcul du pitch et du roll à partir des données filtrées
- Calibration automatique au démarrage
- Contrôle plus stable et intuitif
- Correction des freezes et du game over
- Système de vies + LEDs indicatrices
- Code refactorisé et propre

---

## ✨ Fonctionnalités

- 🎮 Contrôle du serpent par inclinaison (IMU ICM-20948)
- 💡 Affichage sur panneau(x) LED WS2812 (adressables)
- ❤️ Système de 3 vies avec LEDs indicatrices
- 🍏 Génération aléatoire des pommes
- ⚡ Accélération progressive du jeu
- 🔊 Effets sonores (manger, perdre une vie, game over)
- ❌ Affichage Game Over (croix rouge)
- 🔁 Bouton de redémarrage (restart)
- 🧠 Filtre passe-bas sur l’accéléromètre

---

## 🛠️ Matériel utilisé

- Arduino Uno (ou compatible)
- Panneau(x) LED WS2812 (8×8)
- IMU ICM-20948 (I2C)
- Buzzer passif
- 3 LEDs + résistances 220Ω (indicateur de vies)
- Bouton poussoir
- Breadboard + câbles Dupont

---

## 🔌 Câblage (configuration actuelle de test)

### IMU ICM-20948 (I2C)
| Arduino | IMU |
|--------|-----|
| 5V | VCC / VIN |
| GND | GND |
| A4 | SDA / DA |
| A5 | SCL / CL |

> Adresse I2C utilisée : `0x69` (AD0 = HIGH)

### LEDs de vies
| Arduino | Fonction |
|--------|----------|
| D3 | Vie 1 |
| D4 | Vie 2 |
| D5 | Vie 3 |

### Buzzer
| Arduino | Buzzer |
|--------|--------|
| D9 | + |
| GND | - |

### Bouton Restart
| Arduino | Bouton |
|--------|--------|
| D2 | Bouton |
| GND | GND (INPUT_PULLUP) |

### Affichage LED (WS2812)
| Arduino | LED |
|--------|-----|
| D6 | DIN |
| 5V | VCC |
| GND | GND |

---

## 📦 Bibliothèques Arduino

À installer via le gestionnaire de bibliothèques :

- `Adafruit NeoPixel`
- `SparkFun ICM-20948 Arduino Library`
- `Wire` (incluse par défaut)

---

## 🚀 Installation

```bash
git clone https://github.com/leabugnon/snake-arduino.git
