# Station Température & Hygrométrie connectée pour Home Studio & Lutherie 🎸🌡️

Une station de mesure intelligente, économe en énergie et ultra-précise, spécialement conçue pour surveiller l'environnement des instruments de musique (guitares, violons, bois massifs) et des home studios. Développée sur **FireBeetle ESP8266** avec un capteur **DHT22** et un écran **OLED SSD1306 (0.96")**, elle intègre une gestion avancée de l'autonomie sur batterie et un affichage multi-pages complet.

---

## 🎯 Pourquoi ce projet pour les musiciens et luthiers ?

Les instruments à cordes et en bois massif (guitares acoustiques, classiques, violons, cellos) réagissent instantanément aux variations hygrométriques :

* **En dessous de 40 % d'humidité** : Le bois se rétracte, entraînant des risques majeurs de fissures de la table d'harmonie, de frisage ou de décollement des frettes.
* **Au-dessus de 60 % d'humidité** : Le bois gonfle, la colle animale ramollit, et le risque de moisissure ou de condensation apparaît.

Cette station de monitoring offre un suivi permanent de ces paramètres critiques pour éviter les dommages irréversibles sur vos instruments précieux.

---

## ✨ Fonctionnalités principales

* **⚡ Autonomie optimisée & Réactivité instantanée** : Grâce à un sommeil léger intelligent sans interruption cadencée, l'ESP8266 se réveille instantanément dès l'appui sur le bouton tout en préservant la batterie.
* **📈 Graphique d'historique sur 12h** : Visualisez en un clin d'œil les tendances de température et d'humidité des dernières heures (avec repères visuels de la zone idéale 45-55 %).
* **💧 Calcul du Point de Rosée** : Évaluez le risque de condensation sur les parois froides ou à proximité des instruments.
* **⚠️ Suivi du Stress Hydrique** : Affiche le pourcentage exact de temps passé hors des zones de sécurité pour quantifier l'exposition au danger (trop sec / trop humide).
* **🚨 Historique des Pics et Alertes** : Garde en mémoire l'intensité et l'ancienneté des pires pics de sécheresse ou d'humidité enregistrés.
* **🔋 Surveillance de la batterie** : Indicateur de tension en temps réel, jauge graphique, alerte de batterie faible et arrêt protecteur en cas de seuil critique.

---

## 🖥️ Navigation sur l'écran (6 Pages)

Un appui court sur le bouton poussoir allume l'écran et permet de faire défiler les pages suivantes :

1. **PAGE 0 : Lecture immédiate & Graphique**
* Température et humidité en grands chiffres avec flèche de tendance.
* Graphique d'historique sur la hauteur de l'écran (courbe d'humidité et points de température).
* Changement visuel du fond en cas d'alerte.


2. **PAGE 1 : Statistiques globales**
* Température et humidité (Min / Moyenne / Max).
* Durée de la période couverte et taux de présence dans la plage idéale.


3. **PAGE 2 : Point de rosée**
* Valeur actuelle, moyenne et extrêmes du point de rosée.
* Pied de page dynamique évaluant le risque pour les instruments.


4. **PAGE 3 : Stress hydrique (Temps hors limites)**
* Pourcentage du temps passé en zone sécurisée (40-60 %), en zone sèche (<40 %) et en zone humide (>60 %).


5. **PAGE 4 : Pics & Alertes**
* Valeur de la pire sécheresse et de la pire humidité enregistrées, avec leur ancienneté précise (ex: *"il y a 2h"*).


6. **PAGE 5 : Diagnostic système**
* Tension précise de la batterie (`V`), état de santé du capteur DHT22, temps de veille et durée de fonctionnement cumulée (*Uptime*).



---

## 🛠️ Composants Matériels (Hardware)

* **Microcontrôleur** : DFRobot FireBeetle ESP8266 (ou NodeMCU ESP8266 V2).


* **Capteur** : DHT22 (AM2302) – Température et Hygrométrie haute précision.


* **Afficheur** : Écran OLED 0.96" SSD1306 (128×64 pixels, protocole I2C).


* **Alimentation** : Batterie Li-Po (avec gestion de la tension sur ADC).
* **Interface** : Bouton poussoir tactile.

---

## 🔌 Schéma de Câblage

| Composant | Broche Composant | Broche ESP8266 |
| --- | --- | --- |
| **Écran OLED (I2C)** | SCL | GPIO 5 (D1)

 |
|  | SDA | GPIO 4 (D2)

 |
| **Capteur DHT22** | DATA | GPIO 10 |
| **Bouton Poussoir** | Signal / Pull-up | GPIO 12 (autre borne à GND) |

---

## 🚀 Installation & Utilisation

1. Clonez ce dépôt ou téléchargez le fichier source `.ino`.
2. Ouvrez le projet avec l'IDE Arduino.
3. Installez les bibliothèques requises via le gestionnaire de bibliothèques :
* `Adafruit GFX Library`
* `Adafruit SSD1306`
* `DHT sensor library` (par Adafruit)


4. Sélectionnez votre carte ESP8266, compilez et téléversez le code sur votre module.
5. Intégrez l'ensemble dans votre boîtier imprimé en 3D !
