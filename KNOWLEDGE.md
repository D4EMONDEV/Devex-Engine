# Devex Engine - Documentation Complète de l'API

> **Documentation exhaustive de toutes les APIs C++ et C# du moteur Devex Engine**
> *Généré automatiquement à partir du code source - Version: 2026-10-04*

---

## 📖 Table des Matières

1. [Introduction et Architecture Générale](#1-introduction-et-architecture-générale)
   - [1.1. Overview du Moteur Devex Engine](#11-overview-du-moteur-devex-engine)
   - [1.2. Architecture Logicielle](#12-architecture-logicielle)
   - [1.3. Structure des Dossiers](#13-structure-des-dossiers)
   - [1.4. Concepts Fondamentaux](#14-concepts-fondamentaux)
2. [Système ECS (Entity-Component-System)](#2-système-ecs-entity-component-system)
   - [2.1. Concepts de Base](#21-concepts-de-base)
   - [2.2. Avantages de cette Architecture](#22-avantages-de-cette-architecture)
   - [2.3. Comparaison ECS vs OOP Traditionnelle](#23-comparaison-ecs-vs-oop-traditionnelle)
   - [2.4. Pattern d'Utilisation Typique](#24-pattern-dutilisation-typique)
3. [API C++ Complète](#3-api-c-complète)
   - [3.1. Système de Scène et Entités](#31-système-de-scène-et-entités)
4. [API C# Complète](#4-api-c-complète)
   - [4.1. Système de Scène et Entités](#41-système-de-scène-et-entités)
   - [4.2. Composants C#](#42-composants-c)
   - [4.3. Accès aux Composants](#43-accès-aux-composants)
   - [4.4. Systèmes de Jeu et Runtime](#44-systèmes-de-jeu-et-runtime)
   - [4.5. Mathématiques](#45-mathématiques)
   - [4.6. Input (Entrées)](#46-input-entrées)
5. [Tutoriels Pratiques](#5-tutoriels-pratiques)
   - [5.1. Création d'un Projet de Base](#51-création-dun-projet-de-base)
   - [5.2. Projet 2D - Plateformer Simple](#52-projet-2d---plateformer-simple)
   - [5.3. Projet 3D - Véhicule Arcade](#53-projet-3d---véhicule-arcade)
   - [5.4. Système ECS Expliqué en Profondeur](#54-système-ecs-expliqué-en-profondeur)
6. [Exemples de Code](#6-exemples-de-code)
   - [6.1. Exemples C++](#61-exemples-c)
   - [6.2. Exemples C#](#62-exemples-c)
7. [Références Techniques](#7-références-techniques)
   - [7.1. Types de Données et Conventions](#71-types-de-données-et-conventions)
   - [7.2. Conventions de Codage](#72-conventions-de-codage)
   - [7.3. Gestion de la Mémoire](#73-gestion-de-la-mémoire)
8. [Index des Classes et Fonctions](#8-index-des-classes-et-fonctions)
   - [8.1. Index par Module C++](#81-index-par-module-c)
9. [API C++ Complète - Modules Avancés](#9-api-c-complète---modules-avancés)
   - [9.1. Assets et Gestion des Ressources](#91-assets-et-gestion-des-ressources)
   - [9.2. UI et Interface Utilisateur](#92-ui-et-interface-utilisateur)
   - [9.3. Animation](#93-animation)
   - [9.4. Navigation](#94-navigation)
   - [9.5. Particules](#95-particules)
   - [9.6. Audio](#96-audio)
   - [9.7. Rendu Vulkan](#97-rendu-vulkan)
10. [API C# Complète - Services et Systèmes Avancés](#10-api-c-complète---services-et-systèmes-avancés)
   - [10.1. Services (`Devex`, `Services.cs`)](#101-services-devex-servicescs)
   - [10.2. Coroutines et Tweens](#102-coroutines-et-tweens)
   - [10.3. Sauvegardes et réglages (`Saves.cs`)](#103-sauvegardes-et-réglages-savescs)
   - [10.4. Entrées et actions (`Input.cs`)](#104-entrées-et-actions-inputcs)
   - [10.5. Interopération](#105-interopération)
11. [Conclusion et Résumé Final](#11-conclusion-et-résumé-final)
   - [11.1. Résumé de la Documentation](#111-résumé-de-la-documentation)
   - [11.2. Statistiques de la Documentation](#112-statistiques-de-la-documentation)
   - [11.3. Architecture Complète du Moteur](#113-architecture-complète-du-moteur)
   - [11.4. Flow de Développement Recommandé](#114-flow-de-développement-recommandé)
   - [11.5. Bonnes Pratiques de Développement](#115-bonnes-pratiques-de-développement)
   - [11.6. Patterns de Conception Recommandés](#116-patterns-de-conception-recommandés)

---

## 1. Introduction et Architecture Générale

### 1.1. Overview du Moteur Devex Engine

**Devex Engine** est un moteur de jeu moderne basé sur une architecture **ECS (Entity-Component-System)** avec un système de composants flexible et extensible. Il offre :

- **Double API** : C++ pour les performances et C# pour la productivité
- **Système ECS pur** : Entités légères, composants comme données, systèmes comme logique
- **Support multi-plateforme** : Windows (principal), avec architecture portable
- **Rendu Vulkan** : Backend graphique moderne et performant
- **Physique 3D et 2D** : Jolt Physics pour 3D, Box2D pour 2D
- **Animation avancée** : Système d'animation par clips et states
- **Audio** : Système audio complet avec Miniaudio
- **Une seule interface** : `Devex::Ui` dessine les interfaces des jeux et l'éditeur
- **Asset Management** : Pipeline de chargement et gestion des ressources
- **Hot Reloading** : Rechargement à chaud des modules de jeu

### 1.2. Architecture Logicielle

```
┌─────────────────────────────────────────────────────────────┐
│                      DEVEX ENGINE                              │
├─────────────────────────────────────────────────────────────┤
│                                                                 │
│  ┌─────────────┐    ┌─────────────┐    ┌─────────────┐    │
│  │   C++ Core   │    │   C# Managed │    │    Editor    │    │
│  │   Engine     │    │   Runtime    │    │    (C++)     │    │
│  └──────┬───────┘    └──────┬───────┘    └──────┬───────┘    │
│         │                  │                   │                │
│         └──────────────────┼───────────────────┘                │
│                            │                                        │
│                    ┌───────▼───────┐                                │
│                    │ Game Modules  │  (DLLs chargeables dynamiquement) │
│                    │ (C++/C# Code) │                                │
│                    └───────┬───────┘                                │
│                            │                                        │
│         ┌──────────────────┴───────────────────┐                │
│         │                 SCENE                  │                │
│         │  ┌─────────┐  ┌─────────┐  ┌─────────┐  │                │
│         │  │ ENTITY  │  │COMPONENT│  │  SYSTEM  │  │                │
│         │  │  (ID)   │──│  (Data)  │──▶│ (Logic)  │  │                │
│         │  └─────────┘  └─────────┘  └─────────┘  │                │
│         └────────────────────────────────────────┘                │
│                                                                 │
└─────────────────────────────────────────────────────────────┘
```

### 1.3. Structure des Dossiers

```
devex-engine/
├── engine/                          # Moteur C++ principal
│   ├── include/devex/             # En-têtes publics
│   │   ├── core/                 # Types et utilitaires de base
│   │   ├── math/                 # Mathématiques (GLM)
│   │   ├── scene/                # Système ECS et scènes
│   │   ├── physics/              # Physique 3D (Jolt)
│   │   ├── physics2d/            # Physique 2D (Box2D)
│   │   ├── audio/                # Audio (Miniaudio)
│   │   ├── render/               # Rendu (Vulkan)
│   │   ├── animation/            # Animation
│   │   ├── particles/            # Système de particules
│   │   ├── navigation/           # Navigation (NavMesh)
│   │   ├── ui/                  # Interface utilisateur
│   │   ├── runtime/              # Runtime et systèmes de jeu
│   │   ├── platform/             # Abstraction plateforme
│   │   ├── asset/                # Gestion des assets
│   │   ├── reflection/           # Système de réflexion
│   │   └── serialization/        # Sérialisation
│   └── src/                      # Implémentations
│
├── managed/                        # Code C# Managed
│   └── Devex.Managed/            # Bibliothèque C# principale
│       ├── Entity.cs            # Entité C#
│       ├── Component.cs         # Composant C# de base
│       ├── GameRuntime.cs       # Runtime des jeux C#
│       ├── Services.cs          # Services (Input, Audio, etc.)
│       └── ...                  # Autres fichiers
│
├── apps/                          # Applications
│   ├── editor/                   # Éditeur
│   └── player/                   # Joueur (standalone)
│
├── cmake/                         # Configuration CMake
└── out/                           # Fichiers de build
```

### 1.4. Concepts Fondamentaux

#### 1.4.1. Entity-Component-System (ECS)
- **Entité** : Simple identificateur (Handle + UUID), ne contient aucune donnée
- **Composant** : Données seulement, pas de logique. Structure simple (POD-like)
- **Système** : Logique seulement, traite les entités ayant certains composants
- **Scène** : Conteneur d'entités, composants et hiérarchie

#### 1.4.2. Cycle de Vie du Jeu
```
Initialisation → [Start] → [FixedUpdate] → [Update] → [Rendu] → ... → Cleanup
```

#### 1.4.3. Phases de Système
- **Start** : Appelée une fois au début
- **FixedUpdate** : Appelée à intervalle fixe (physique, simulation)
- **Update** : Appelée chaque frame (input, caméras, logique visible)

---

## 2. Système ECS (Entity-Component-System)

### 2.1. Concepts de Base

Le système ECS de Devex Engine est conçu pour être **ultra-performant** tout en restant **simple à utiliser**.

#### Entités
- Représentées par `devex::scene::Entity` en C++
- Simple `Handle` (index + génération) pour l'exécution
- `UUID` pour l'identification persistante (sauvegarde, copie, etc.)
- **Aucune donnée** stockée directement dans l'entité
- Créées et détruites dynamiquement

#### Composants
- Structures simples (POD - Plain Old Data)
- Chaque entité peut avoir **0 ou 1 instance** de chaque type de composant
- Stockés de manière **contiguë par type** pour l'optimisation cache
- Enregistrés via le système de réflexion

#### Systèmes
- Fonctions ou classes qui traitent les entités
- Accèdent aux composants via des **vues (Views)**
- Exécutés dans des phases spécifiques (Start, FixedUpdate, Update)

### 2.2. Avantages de cette Architecture

1. **Performance** : Accès mémoire optimisé (cache-friendly)
2. **Flexibilité** : Composition libre des entités
3. **Extensibilité** : Ajout facile de nouveaux composants et systèmes
4. **Maintenabilité** : Séparation claire données/logique
5. **Sérialisation** : Facile à sauvegarder/charger
6. **Hot Reloading** : Rechargement des systèmes sans perdre l'état

### 2.3. Comparaison ECS vs OOP Traditionnelle

| Aspect | OOP Traditionnelle | ECS Devex |
|--------|-------------------|-----------|
| **Structure** | Hiérarchie de classes | Composition libre |
| **Performance** | Cache misses fréquents | Cache-friendly |
| **Flexibilité** | Héritage rigide | Composition flexible |
| **Mémoire** | Objets dispersés | Données contiguës |
| **Extensibilité** | Modification difficile | Ajout facile |
| **Sérialisation** | Complexe | Naturelle |

### 2.4. Pattern d'Utilisation Typique

#### En C++
```cpp
// Définition d'un composant
struct Health {
    float current = 100.0f;
    float max = 100.0f;
};
DEVEX_DECLARE_ENGINE_REFLECTION(Health);

// Enregistrement du composant
DEVEX_REFLECT(Health) {
    type.field("current", &Health::current);
    type.field("max", &Health::max);
}

// Système qui traite les entités avec Health
void updateHealth(SystemContext& context) {
    auto view = context.scene.view<Health>();
    for (auto [entity, health] : view) {
        // Logique de mise à jour
        if (health.current <= 0) {
            context.scene.destroyEntity(entity);
        }
    }
}
```

#### En C#
```csharp
// Composant C#
public class PlayerController : Component
{
    public float Speed = 5.0f;
    public float JumpForce = 10.0f;
    
    public override void Update(float delta)
    {
        // Logique de déplacement
        var input = Input.ActionAxis("Horizontal");
        Entity.Transform.Position.X += input * Speed * delta;
    }
}
```

---

## 3. API C++ Complète

### 3.1. Système de Scène et Entités

#### 3.1.1. Types Fundamentaux

##### Entity (`devex::scene::Entity`)
```cpp
// Dans: engine/include/devex/scene/Entity.hpp

namespace devex::scene {
    struct EntityTag;
    
    // Référence compacte à une entité d'une scène
    // Devient invalide lorsque l'entité est détruite
    using Entity = core::Handle<EntityTag>;
}
```

**Caractéristiques :**
- `Handle` : index (32 bits) + génération (32 bits) = 64 bits total
- Détection automatique des entités détruites via le système de génération
- **Ne contient aucune donnée** - seulement un identifiant

##### EntityRef (`devex::scene::EntityRef`)
```cpp
// Dans: engine/include/devex/scene/EntityRef.hpp

namespace devex::scene {
    struct DEVEX_API EntityRef {
        core::Uuid uuid;
        
        [[nodiscard]] bool isNil() const noexcept { return uuid.isNil(); }
        bool operator==(const EntityRef&) const = default;
    };
}
```

**Utilisation :**
- Référence persistante entre les scènes et les sauvegardes
- Basée sur UUID au lieu de Handle
- Résolue via `Scene::resolve(EntityRef)`

#### 3.1.2. Scène (`devex::scene::Scene`)

```cpp
// Dans: engine/include/devex/scene/Scene.hpp

namespace devex::scene {
    enum class SceneKind : std::uint8_t { ThreeD, TwoD };
    
    class DEVEX_API Scene {
    public:
        Scene();
        Scene(Scene&& other) noexcept;
        Scene& operator=(Scene&& other) noexcept;
        ~Scene();
        
        // Interdit la copie (utiliser clone())
        Scene(const Scene&) = delete;
        Scene& operator=(const Scene&) = delete;
        
        // Création d'une copie complète
        [[nodiscard]] Scene clone() const;
        
        // Gestion des entités
        [[nodiscard]] Entity createEntity(std::string name = {});
        [[nodiscard]] core::Result<Entity> createEntity(core::Uuid uuid, std::string name);
        void destroyEntity(Entity entity);
        
        [[nodiscard]] bool isAlive(Entity entity) const noexcept;
        [[nodiscard]] std::size_t entityCount() const noexcept;
        [[nodiscard]] Entity findEntity(core::Uuid uuid) const noexcept;
        [[nodiscard]] Entity entityAtIndex(std::uint32_t index) const noexcept;
        
        // Identification des entités
        [[nodiscard]] core::Uuid uuid(Entity entity) const noexcept;
        [[nodiscard]] Entity resolve(EntityRef reference) const noexcept;
        [[nodiscard]] EntityRef reference(Entity entity) const noexcept;
        
        // Gestion des noms
        [[nodiscard]] const std::string& name(Entity entity) const noexcept;
        void setName(Entity entity, std::string name);
        
        // Hiérarchie
        [[nodiscard]] core::Result<void> setParent(Entity child, Entity parent, Entity before = {});
        void placeLast(Entity child);
        [[nodiscard]] Entity parent(Entity entity) const noexcept;
        [[nodiscard]] Entity firstChild(Entity entity) const noexcept;
        [[nodiscard]] Entity nextSibling(Entity entity) const noexcept;
        [[nodiscard]] Entity firstRoot() const noexcept;
        
        // Gestion des composants (template)
        template <typename T, typename... Args>
        T& add(Entity entity, Args&&... args);
        
        template <typename T>
        [[nodiscard]] T* tryGet(Entity entity) noexcept;
        
        template <typename T>
        [[nodiscard]] const T* tryGet(Entity entity) const noexcept;
        
        template <typename T>
        [[nodiscard]] T& get(Entity entity) noexcept;
        
        template <typename T>
        [[nodiscard]] const T& get(Entity entity) const noexcept;
        
        template <typename T>
        [[nodiscard]] bool has(Entity entity) const noexcept;
        
        template <typename T>
        void remove(Entity entity);
        
        // Vues sur les composants
        template <typename... Components>
        [[nodiscard]] View<Components...> view() noexcept;
        
        template <typename... Components>
        [[nodiscard]] View<const Components...> view() const noexcept;
        
        // Mise à jour des transformations
        void updateTransforms();
        
        // Type de scène
        [[nodiscard]] SceneKind kind() const noexcept;
        void setKind(SceneKind kind) noexcept;
        
        // Gestion des pools de composants dynamiques
        [[nodiscard]] DynamicComponentPool& dynamicPool(
            std::size_t typeIndex, 
            const std::shared_ptr<const DynamicComponentLayout>& layout);
        
        [[nodiscard]] DynamicComponentPool* dynamicPool(std::size_t typeIndex) noexcept;
        [[nodiscard]] const DynamicComponentPool* dynamicPool(std::size_t typeIndex) const noexcept;
        
        [[nodiscard]] std::size_t componentPoolCount() const noexcept;
        [[nodiscard]] const ComponentPoolBase* componentPool(std::size_t typeIndex) const noexcept;
        void destroyComponentPool(std::size_t typeIndex) noexcept;
    };
}
```

#### 3.1.3. Exemple d'Utilisation de Scene

```cpp
// Création d'une scène
Scene scene;
scene.setKind(SceneKind::ThreeD);

// Création d'entités
Entity player = scene.createEntity("Player");
Entity enemy = scene.createEntity("Enemy");

// Ajout de composants
scene.add<Transform>(player, Transform{.position = {0, 0, 0}});
scene.add<Transform>(enemy, Transform{.position = {5, 0, 0}});

// Accès aux composants
Transform& playerTransform = scene.get<Transform>(player);
playerTransform.position.x += 1.0f;

// Utilisation de vue
for (auto [entity, transform] : scene.view<Transform>()) {
    std::cout << "Entity " << scene.name(entity) << " at " 
              << transform.position.x << ", " << transform.position.y << std::endl;
}

// Hiérarchie
scene.setParent(enemy, player); // enemy devient enfant de player

// Destruction
scene.destroyEntity(enemy);
```

#### 3.1.4. ComponentPool (`devex::scene::ComponentPool`)

```cpp
// Dans: engine/include/devex/scene/ComponentPool.hpp

namespace devex::scene {
    class DEVEX_API ComponentPoolBase {
    public:
        virtual ~ComponentPoolBase() = default;
        
        [[nodiscard]] virtual std::unique_ptr<ComponentPoolBase> clone() const = 0;
        [[nodiscard]] virtual const void* moduleAnchor() const noexcept = 0;
        
        [[nodiscard]] bool contains(Entity entity) const noexcept;
        [[nodiscard]] std::size_t size() const noexcept;
        [[nodiscard]] std::span<const Entity> entities() const noexcept;
        
        virtual void remove(Entity entity) = 0;
    };
    
    template <typename T>
    class ComponentPool final : public ComponentPoolBase {
    public:
        template <typename... Args>
        T& emplace(Entity entity, Args&&... args);
        
        [[nodiscard]] T* find(Entity entity) noexcept;
        [[nodiscard]] const T* find(Entity entity) const noexcept;
        
        [[nodiscard]] std::unique_ptr<ComponentPoolBase> clone() const override;
        [[nodiscard]] const void* moduleAnchor() const noexcept override;
        
        void remove(Entity entity) override;
    };
}
```

**Fonctionnement interne :**
- **Sparse Set** : Mapping entités → indices denses
- Stockage contigu des composants pour optimisation cache
- Suppression en O(1) avec permutation du dernier élément

#### 3.1.5. View (`devex::scene::View`)

```cpp
// Dans: engine/include/devex/scene/View.hpp

namespace devex::scene {
    template <typename... Components>
    class View {
    public:
        using Pools = std::tuple<ComponentPool<std::remove_const_t<Components>>*...>;
        
        explicit View(Pools pools) noexcept;
        
        class Iterator {
        public:
            using value_type = std::tuple<Entity, Components&...>;
            
            Iterator(const View* view, std::size_t index) noexcept;
            
            [[nodiscard]] value_type operator*() const noexcept;
            Iterator& operator++() noexcept;
            [[nodiscard]] bool operator==(const Iterator& other) const noexcept;
        };
        
        [[nodiscard]] Iterator begin() const noexcept;
        [[nodiscard]] Iterator end() const noexcept;
    };
}
```

**Caractéristiques :**
- Itère uniquement sur les entités ayant **tous** les composants spécifiés
- Se base sur le plus petit pool pour minimiser les vérifications
- Accès direct et efficace aux données

**Exemple :**
```cpp
// Vue sur les entités avec Transform et RigidBody
for (auto [entity, transform, body] : scene.view<Transform, RigidBody>()) {
    // Traiter les entités physiques
    body.linearVelocity += transform.position * 0.1f;
}

// Vue const pour lecture seule
for (auto [entity, transform] : scene.view<const Transform>()) {
    std::cout << "Position: " << transform.position << std::endl;
}
```

#### 3.1.6. ComponentRegistry (`devex::scene::ComponentRegistry`)

```cpp
// Dans: engine/include/devex/scene/ComponentRegistry.hpp

namespace devex::scene {
    struct DEVEX_API ComponentType {
        const reflection::TypeInfo* type = nullptr;
        std::size_t index = 0;
        std::function<void*(Scene& scene, Entity entity)> emplace;
        std::function<const void*(const Scene& scene, Entity entity)> find;
        std::function<void*(Scene& scene, Entity entity)> findMutable;
        std::function<void(Scene& scene, Entity entity)> remove;
        std::shared_ptr<const DynamicComponentLayout> layout;
        
        [[nodiscard]] std::string_view name() const noexcept;
    };
    
    class DEVEX_API ComponentRegistry {
    public:
        template <typename T>
        void add();
        
        bool addDynamic(std::shared_ptr<const DynamicComponentLayout> layout);
        bool remove(std::string_view name);
        
        [[nodiscard]] const ComponentType* find(std::string_view name) const noexcept;
        [[nodiscard]] const ComponentType* findByIndex(std::size_t index) const noexcept;
        
        [[nodiscard]] std::span<const ComponentType> types() const noexcept;
        [[nodiscard]] std::uint64_t generation() const noexcept;
    };
    
    [[nodiscard]] DEVEX_API ComponentRegistry& componentRegistry();
    
    template <typename T>
    void registerComponent() {
        componentRegistry().add<T>();
    }
}
```

**Utilisation :**
```cpp
// Enregistrement des composants du moteur
void registerEngineComponents() {
    registerComponent<Transform>();
    registerComponent<MeshRenderer>();
    registerComponent<RigidBody>();
    // ... etc
}
```

---

## 4. API C# Complète

### 4.1. Système de Scène et Entités

#### 4.1.1. Entity (C#)

```csharp
// Dans: managed/Devex.Managed/Entity.cs

namespace Devex;

/// <summary>
/// Une entité de la scène qui joue : le handle que le moteur utilise.
/// Devient invalide lorsque l'entité est détruite, ce que IsAlive indique.
/// </summary>
[StructLayout(LayoutKind.Sequential)]
public readonly struct Entity : IEquatable<Entity>
{
    public readonly uint Index;
    public readonly uint Generation;
    
    /// <summary>Aucune entité.</summary>
    public static Entity None => default;
    
    /// <summary>Si le handle désigne une entité, vivante ou non.</summary>
    public bool IsValid => Generation != 0 && Index != uint.MaxValue;
    
    /// <summary>Si l'entité existe toujours dans la scène.</summary>
    public bool IsAlive => IsValid && Scene.Current.IsAlive(this);
    
    // Propriétés
    public string Name { get => Scene.Current.Name(this); set => Scene.Current.SetName(this, value); }
    public ref Transform Transform => ref Scene.Current.TransformOf(this);
    public bool HasTransform => Scene.Current.HasTransform(this);
    public Vec3 WorldPosition => Scene.Current.WorldPosition(this);
    public System.Numerics.Matrix4x4 WorldMatrix => Scene.Current.WorldMatrix(this);
    public Entity Parent => Scene.Current.Parent(this);
    public IEnumerable<Entity> Children => Scene.Current.Children(this);
    public Uuid Uuid => Scene.Current.UuidOf(this);
    
    // Méthodes
    public void SetParent(Entity parent) => Scene.Current.SetParent(this, parent);
    public void Destroy() => Scene.Current.Destroy(this);
    
    // Création
    public static Entity Create(string name) => Scene.Current.CreateEntity(name);
    public static Entity Find(string name) => Scene.Current.Find(name);
    
    // Équivalence
    public bool Equals(Entity other) => IsValid ? Index == other.Index && Generation == other.Generation : !other.IsValid;
    public override bool Equals(object? other) => other is Entity entity && Equals(entity);
    public override string ToString() => IsValid ? $"Entity({Index}, {Generation})" : "Entity(None)";
    
    public static bool operator ==(Entity left, Entity right) => left.Equals(right);
    public static bool operator !=(Entity left, Entity right) => !left.Equals(right);
}

```

### 4.2. Composants C#

#### 4.2.1. Component (Classe de Base)

```csharp
// Dans: managed/Devex.Managed/Component.cs

namespace Devex;

/// <summary>
/// Un composant d'une entité, écrit en C#.
/// Ses champs publics sont sauvegardés dans les scènes et édités dans l'inspecteur.
/// Start, Update et FixedUpdate s'exécutent pour chaque entité qui l'a.
/// </summary>
public abstract class Component
{
    /// <summary>L'entité à laquelle ce composant appartient.</summary>
    public Entity Entity { get; internal set; }
    
    /// <summary>La scène dans laquelle vit l'entité.</summary>
    public Scene Scene { get; internal set; } = null!;
    
    /// <summary>Appelé une fois, avant la première mise à jour.</summary>
    public virtual void Start() { }
    
    /// <summary>Appelé une fois par frame.</summary>
    public virtual void Update(float delta) { }
    
    /// <summary>Appelé au rythme fixe avant la physique.</summary>
    public virtual void FixedUpdate(float delta) { }

    /// <summary>Après animation et interpolation physique, pour le suivi de caméra et les visuels.</summary>
    public virtual void LateUpdate(float delta) { }
    
    // Méthodes de collision
    public virtual void OnCollisionEnter(Entity other) { }
    public virtual void OnCollisionExit(Entity other) { }
    public virtual void OnTriggerEnter(Entity other) { }
    public virtual void OnTriggerExit(Entity other) { }
    
    // Accesseurs
    public ref Transform Transform => ref Entity.Transform;
    public void DestroyEntity() => Entity.Destroy();
    
    internal bool Started;
}
```

#### 4.2.2. Attributs Importants

```csharp
[AttributeUsage(AttributeTargets.Field)]
public sealed class AngleAttribute : Attribute; // Champ float en degrés

[AttributeUsage(AttributeTargets.Field)]
public sealed class ColorAttribute : Attribute; // Vec3/Vec4 comme couleur

[AttributeUsage(AttributeTargets.Field)]
public sealed class PhysicsLayerAttribute : Attribute; // uint pour couche de collision

[AttributeUsage(AttributeTargets.Field)]
public sealed class AudioGroupAttribute : Attribute; // uint pour groupe audio

[AttributeUsage(AttributeTargets.Field)]
public sealed class AssetTypeAttribute(string type) : Attribute; // Limite AssetId à un type

[AttributeUsage(AttributeTargets.Field)]
public sealed class HiddenAttribute : Attribute; // Champ non sauvegardé

[AttributeUsage(AttributeTargets.Method)]
public sealed class GameSystemAttribute(SystemPhase phase = SystemPhase.Update) : Attribute
{
    public SystemPhase Phase { get; } = phase;
    public int Order { get; init; }
}
```


### 4.3. Accès aux Composants

#### 4.3.1. NativeComponentAccess (Composants C++ depuis C#)

```csharp
// Accès aux composants natifs (C++) depuis C#
public static unsafe class NativeComponentAccess
{
    public static T Get<T>(this Entity entity) 
        where T : IComponentView<T>, allows ref struct;
    
    public static bool TryGet<T>(this Entity entity, out T view) 
        where T : IComponentView<T>, allows ref struct;
    
    public static bool Has<T>(this Entity entity) 
        where T : IComponentView<T>, allows ref struct;
    
    public static T Add<T>(this Entity entity) 
        where T : IComponentView<T>, allows ref struct;
    
    public static void Remove<T>(this Entity entity) 
        where T : IComponentView<T>, allows ref struct;
}
```

#### 4.3.2. ManagedComponentAccess (Composants C#)

```csharp
// Accès aux composants managés (C#)
public static class ManagedComponentAccess
{
    public static T? Get<T>(this Entity entity) where T : Component => 
        GameRuntime.GetInstance(typeof(T), entity) as T;
    
    public static bool Has<T>(this Entity entity) where T : Component => 
        GameRuntime.GetInstance(typeof(T), entity) != null;
    
    public static T Add<T>(this Entity entity) where T : Component => 
        (T)GameRuntime.AddComponent(typeof(T), entity);
    
    public static void Remove<T>(this Entity entity) where T : Component => 
        GameRuntime.RemoveComponent(typeof(T), entity);
}
```

### 4.4. Systèmes de Jeu et Runtime

#### 4.4.1. GameRuntime (Moteur d'exécution C#)

```csharp
public static unsafe class GameRuntime
{
    public static Scene CurrentScene => SceneView;
    
    // Chargement/déchargement
    public static string Load(string assemblyPath);
    public static void Unload();
    
    // Exécution
    public static void RunPhase(void* scene, SystemPhase phase, float delta);
    
    // Composants
    public static Component? GetInstance(Type type, Entity entity);
    public static IEnumerable<T> Instances<T>() where T : Component;
    public static Component AddComponent(Type type, Entity entity);
    public static void RemoveComponent(Type type, Entity entity);
    
    // Layout
    public static void SetTypeLayout(string typeName, nuint typeIndex, int[] offsets);
    public static void ApplyDefaults(string typeName, void* component);
    
    internal static bool InPhase => _running;
}
```

#### 4.4.2. SystemPhase

```csharp
public enum SystemPhase
{
    Start,        // Une fois au démarrage
    FixedUpdate,  // À intervalle fixe (avant physique)
    Update,       // Chaque frame (input, logique de jeu)
    LateUpdate    // Après interpolation physique (caméras, visuels)
}
```

Dans `LateUpdate`, `WorldPosition` et `WorldMatrix` correspondent aux poses affichées.
Une caméra enfant convertit sa position monde avec l'inverse de `Entity.Parent.WorldMatrix`
avant d'écrire dans `Transform.Position`, qui reste une position locale. La physique continue
d'utiliser `Update` et `FixedUpdate` ; `LateUpdate` reste suspendu pendant la pause.

### 4.5. Mathématiques

#### 4.5.1. Types de Base

```csharp
[StructLayout(LayoutKind.Sequential)]
public struct Vec2 { public float X; public float Y; }

[StructLayout(LayoutKind.Sequential)]
public struct Vec3 { public float X; public float Y; public float Z; }

[StructLayout(LayoutKind.Sequential)]
public struct Vec4 { public float X; public float Y; public float Z; public float W; }

[StructLayout(LayoutKind.Sequential)]
public struct Quat { public float X; public float Y; public float Z; public float W; }

[StructLayout(LayoutKind.Sequential)]
public struct Mat4 { /* Matrice 4x4 */ }

[StructLayout(LayoutKind.Sequential)]
public struct Uuid { /* UUID 128-bit */ }

[StructLayout(LayoutKind.Sequential)]
public struct AssetId { /* Identifiant d'asset */ }
```

### 4.6. Input (Entrées)

Les touches (`Key`, dans `Key.cs`), la souris, et surtout les **actions** du projet (Project
Settings > Input), que les contextes activent et que le joueur peut réassigner :

```csharp
// A mouse button, as the engine numbers them.
public enum MouseButton
{
    Left = 0,
    Middle = 1,
    Right = 2,
    X1 = 3,
    X2 = 4,
}

// Keyboard and mouse of the current frame. "Pressed" is true only on the frame of the press.
public static unsafe class Input
{
    public static bool IsKeyDown(Key key);
    public static bool WasKeyPressed(Key key);
    public static bool IsMouseButtonDown(MouseButton button);
    public static bool WasMouseButtonPressed(MouseButton button);
    // How far the mouse moved since the previous frame, in pixels.
    public static Vec2 MouseDelta { get; }
    // Where the mouse is in the window, in pixels.
    public static Vec2 MousePosition { get; }
    // A captured mouse is hidden and reports unbounded motion, as a first-person camera needs.
    public static bool MouseCaptured { get; set; }
    // A button held down; an axis or a vector pushed at least halfway.
    public static bool IsActionDown(string action);
    // During the frame the action went down. Read it from Update rather than FixedUpdate.
    public static bool WasActionPressed(string action);
    // During the frame the action went up.
    public static bool WasActionReleased(string action);
    // From -1 to 1 for an axis, 0 or 1 for a button, 0 for a vector.
    public static float ActionAxis(string action);
    // A direction of length 1 at most, with Y up; an axis along X; zero for a button.
    public static Vec2 ActionVector(string action);
    // Turns a context of actions on or off, such as the actions of the game while a menu is open:
    // the actions of a context that is off read as released. Contexts start as the project says,
    // and keep their state from scene to scene.
    public static void SetContextActive(string context, bool active);
    public static bool IsContextActive(string context);
    // How many bindings the action has, in the order of the project settings.
    public static int BindingCount(string action);
    // What to show for a binding: the key as the keyboard prints it, or the name of the button.
    public static string BindingLabel(string action, int binding);
    // Waits for the next key or button the player presses and binds it to the action, in place of
    // one of its bindings: a keyboard binding takes a key or a mouse button, a gamepad one a
    // gamepad button. Escape gives up. The bindings are saved for the next games.
    public static void ListenForBinding(string action, int binding);
    // Whether a binding waits for a key.
    public static bool IsListeningForBinding { get; }
    public static void StopListeningForBinding();
    // Every binding back to those of the project settings.
    public static void ResetBindings();
}
```


---

## 5. Tutoriels Pratiques

Les exemples complets de cette section et de la section 6 sont extraits de ce document et compilés
avec le moteur par la cible `devex_documentation_examples`, incluse dans le build des tests.
Le C++ est vérifié avec les avertissements traités comme erreurs ; le C# l'est aussi, avec la
nullabilité activée, quand le SDK .NET 10 est disponible. Les quatre scripts du
[guide de démarrage](docs/guide-demarrage-2d-3d.md) sont compilés en même temps.
Les blocs marqués « pseudo-code » servent uniquement à expliquer une idée.

### 5.1. Création d'un Projet de Base

#### 5.1.1. Structure de Projet Minimale

Créer le projet dans le gestionnaire de projets de l'éditeur. Il crée le fichier `.dvxproj` et les
dossiers du projet. Le code du jeu va dans `code/`, où le C++ et le C# peuvent cohabiter :

```text
MonJeu/
├── MonJeu.dvxproj
├── code/
│   ├── CMakeLists.txt
│   ├── PlayerModule.cpp
│   └── PlayerController2D.cs
└── assets/
    └── scenes/
        └── main.dvxscene
```

L'éditeur produit le projet .NET des fichiers `.cs` et compile le module C++ avec le même
compilateur et la même configuration que le moteur. Il fournit `Devex_DIR` à CMake.
Dans les réglages du projet, créer une action `Move` de type Vector et une action `Jump` de type
Button, avec les touches ou les sticks choisis. Les exemples de déplacement ci-dessous les lisent.

#### 5.1.2. CMakeLists.txt pour un Module de Jeu C++

<!-- devex-example: CMakeLists.txt -->
```cmake
cmake_minimum_required(VERSION 3.25)
project(MonJeu LANGUAGES CXX)

find_package(Devex CONFIG REQUIRED)
devex_add_game_module(SOURCES PlayerModule.cpp)
```

`devex_add_game_module` lie `Devex::Engine`, choisit le standard C++ du moteur et produit
`Game.dll` ou `libGame.so` dans `bin/`. Le module exporte son point d'entrée avec
`DEVEX_GAME_MODULE` ; il n'a pas besoin d'exporter tous ses symboles.

#### 5.1.3. Point d'Entrée C++ (PlayerModule.cpp)

Ce système déplace horizontalement les entités qui portent `Transform` et `PlayerController`.
Leur transformation est locale : cet exemple convient à des entités à la racine de la scène.

<!-- devex-example: PlayerModule.cpp -->
```cpp
#include <devex/runtime/Game.hpp>
#include <devex/scene/Components.hpp>

struct PlayerController
{
    float speed = 5.0f;
};
DEVEX_DECLARE_REFLECTION(PlayerController);

DEVEX_REFLECT(PlayerController)
{
    type.field("speed", &PlayerController::speed);
}

void updatePlayer(devex::runtime::SystemContext& context)
{
    if (context.actions == nullptr)
    {
        return;
    }
    const float horizontal = context.actions->vector("Move").x;
    const float seconds = static_cast<float>(context.delta.count());
    for (auto [entity, transform, controller] :
         context.scene.view<devex::scene::Transform, PlayerController>())
    {
        transform.position.x += horizontal * controller.speed * seconds;
    }
}

DEVEX_GAME_MODULE(game)
{
    game.component<PlayerController>();
    game.system("UpdatePlayer", devex::runtime::SystemPhase::Update, &updatePlayer);
}
```

### 5.2. Projet 2D - Plateformer Simple

#### 5.2.1. Architecture d'un Jeu 2D

Le joueur porte un `Transform`, un `CharacterController2D` et le script `PlayerController2D`.
Le contrôleur gère sa capsule, le sol, la gravité et les déplacements. Les objets dynamiques à
pousser utilisent `RigidBody2D` et un collider ; les sols statiques ont un `BoxCollider2D` ou un
`TilemapCollider2D`. Le visuel du joueur peut être un enfant avec `SpriteRenderer`.

Les vues C# des composants natifs, comme `CharacterController2D`, sont des `ref struct` : on les
obtient pendant l'appel qui les utilise, sans les conserver dans les champs d'une classe.

#### 5.2.2. Composant PlayerController2D (C#)

<!-- devex-example: PlayerController2D.cs -->
```csharp
using Devex;

public class PlayerController2D : Component
{
    public float Speed = 5.0f;
    public float JumpSpeed = 7.0f;

    public override void Update(float delta)
    {
        if (!Entity.TryGet<CharacterController2D>(out var controller))
            return;

        Vec2 velocity = controller.Velocity;
        velocity.X = Input.ActionVector("Move").X * Speed;
        if (controller.Grounded && Input.WasActionPressed("Jump"))
            velocity.Y = JumpSpeed;

        // La physique applique la gravité et le pas de temps.
        controller.Velocity = velocity;
    }

    public override void OnCollisionEnter(Entity other)
    {
        Log.Info($"Collision avec {other.Name}");
    }
}
```

#### 5.2.3. Configuration de la Scène 2D

Dans une scène 2D, créer un joueur à la racine, à la position `(0, 2, 0)`, puis lui ajouter
`CharacterController2D` et `PlayerController2D`. Ajouter un enfant avec `SpriteRenderer` pour
l'image du personnage. Créer un sol avec un `BoxCollider2D` de taille `(10, 1)` ; un collider sans
corps physique est statique. Ajouter une caméra orthographique qui regarde vers -Z.
Le [guide de démarrage 2D](docs/guide-demarrage-2d-3d.md) détaille ces opérations et les entrées.

### 5.3. Projet 3D - Véhicule Arcade

#### 5.3.1. Composants Clés pour un Jeu 3D

`Transform`, `MeshRenderer` et `Camera` décrivent la scène visible. Pour les collisions, choisir
un `RigidBody` avec ses colliders, ou un `CharacterController` qui possède sa propre capsule.
L'exemple ci-dessous utilise un `RigidBody` dynamique avec un `BoxCollider` pour un déplacement
arcade : il ne simule ni roues ni suspension. Verrouiller ses rotations X et Z pour qu'il reste
debout. L'entité est à la racine et son avant est -Z, comme celui du moteur.

#### 5.3.2. Composant CarController (C#)

<!-- devex-example: CarController.cs -->
```csharp
using Devex;

public class CarController : Component
{
    public float Speed = 20.0f;
    [Angle] public float RotationSpeed = 2.0f;

    public override void Update(float delta)
    {
        if (!Entity.TryGet<RigidBody>(out var body))
            return;

        Vec2 move = Input.ActionVector("Move");
        Transform.Rotation = Quat.AngleAxis(-move.X * RotationSpeed * delta, Vec3.Up)
            * Transform.Rotation;

        Vec3 velocity = (Transform.Rotation * Vec3.Forward) * (move.Y * Speed);
        velocity.Y = body.LinearVelocity.Y;
        body.LinearVelocity = velocity;
    }
}
```

### 5.4. Système ECS Expliqué en Profondeur

Un ECS range les composants d'un même type dans un tableau contigu. Une vue parcourt les entités
qui possèdent les types demandés ; les systèmes modifient leurs données, puis le renderer en
extrait ce qu'il doit dessiner. Un `MeshRenderer` est une donnée, sans méthode `Draw()`.

**Pseudo-code de comparaison, indépendant de l'API du moteur :**

```text
Objets : suivre le pointeur de chaque objet, puis celui de son composant.
ECS    : parcourir les composants d'une vue et lire leurs données.
```

Le système de la section 5.1 montre une vraie vue. Il n'existe pas ici de mesure comparative
ECS/OOP : les performances mesurées du moteur, avec leurs conditions de mesure, sont dans
[docs/performances.md](docs/performances.md).

---

## 6. Exemples de Code

Les exemples C++ ci-dessous sont des modules de jeu indépendants : chacun déclare son propre
`DEVEX_GAME_MODULE`. Pour les réunir dans un jeu, garder un seul point d'entrée qui enregistre
leurs composants et leurs systèmes.

### 6.1. Exemples C++

#### 6.1.1. Système de Gravité Personnalisée

Les corps concernés sont dynamiques, avec `gravityScale = 0` pour que la gravité du projet ne
s'ajoute pas à celle du volume. La direction de `gravity` est exprimée dans le monde ; le rayon
est en mètres. Les transformations mondiales sont mises à jour avant de mesurer les distances.

<!-- devex-example: GravityModule.cpp -->
```cpp
#include <devex/runtime/Game.hpp>
#include <devex/scene/Components.hpp>
#include <devex/scene/PhysicsComponents.hpp>

struct GravityVolume
{
    devex::math::Vec3 gravity{0.0f, -9.81f, 0.0f};
    float radius = 10.0f;
};
DEVEX_DECLARE_REFLECTION(GravityVolume);

DEVEX_REFLECT(GravityVolume)
{
    type.field("gravity", &GravityVolume::gravity);
    type.field("radius", &GravityVolume::radius);
}

void applyGravity(devex::runtime::SystemContext& context)
{
    auto& scene = context.scene;
    scene.updateTransforms();
    const float seconds = static_cast<float>(context.delta.count());
    for (auto [volumeEntity, volumeTransform, volume] :
         scene.view<devex::scene::WorldTransform, GravityVolume>())
    {
        const devex::math::Vec3 center{volumeTransform.matrix[3]};
        for (auto [bodyEntity, bodyTransform, body] :
             scene.view<devex::scene::WorldTransform, devex::scene::RigidBody>())
        {
            const devex::math::Vec3 position{bodyTransform.matrix[3]};
            if (body.type == devex::scene::BodyType::Dynamic &&
                devex::math::length(position - center) < volume.radius)
            {
                body.linearVelocity += volume.gravity * seconds;
            }
        }
    }
}

DEVEX_GAME_MODULE(game)
{
    game.component<GravityVolume>();
    game.system("ApplyGravity", devex::runtime::SystemPhase::FixedUpdate, &applyGravity);
}
```

#### 6.1.2. Système de Particules Personnalisé

Ces particules sont des entités d'exemple, sans rendu ; pour des effets visibles, utiliser
`ParticleEmitter` et les services de `ParticleWorld` décrits en section 9.5. Les entités expirées
sont détruites après le parcours : détruire un élément pendant l'itération invaliderait la vue.

<!-- devex-example: FireParticlesModule.cpp -->
```cpp
#include <devex/runtime/Game.hpp>
#include <devex/scene/Components.hpp>

#include <vector>

struct FireParticle
{
    devex::math::Vec3 velocity{0.0f, 1.0f, 0.0f};
    float lifetime = 1.0f;
};
DEVEX_DECLARE_REFLECTION(FireParticle);

DEVEX_REFLECT(FireParticle)
{
    type.field("velocity", &FireParticle::velocity);
    type.field("lifetime", &FireParticle::lifetime);
}

void updateFireParticles(devex::runtime::SystemContext& context)
{
    const float seconds = static_cast<float>(context.delta.count());
    std::vector<devex::scene::Entity> expired;
    for (auto [entity, transform, particle] :
         context.scene.view<devex::scene::Transform, FireParticle>())
    {
        transform.position += particle.velocity * seconds;
        particle.lifetime -= seconds;
        if (particle.lifetime <= 0.0f)
        {
            expired.push_back(entity);
        }
    }
    for (const devex::scene::Entity entity : expired)
    {
        context.scene.destroyEntity(entity);
    }
}

void spawnFireParticle(devex::scene::Scene& scene, devex::math::Vec3 position,
                       devex::math::Vec3 velocity)
{
    const auto entity = scene.createEntity("FireParticle");
    scene.add<devex::scene::Transform>(entity).position = position;
    scene.add<FireParticle>(entity).velocity = velocity;
}

DEVEX_GAME_MODULE(game)
{
    game.component<FireParticle>();
    game.system("FireParticles", devex::runtime::SystemPhase::Update, &updateFireParticles);
}
```

#### 6.1.3. Système de Dialogue

Appeler `startDialogue` depuis le jeu pour ouvrir un dialogue. L'action `Jump` passe à la ligne
suivante une seule fois par appui ; une liste vide ne démarre pas de dialogue.

<!-- devex-example: DialogueModule.cpp -->
```cpp
#include <devex/runtime/Game.hpp>

#include <cstdint>
#include <string>
#include <vector>

struct Dialogue
{
    std::vector<std::string> lines;
    std::uint32_t currentLine = 0;
    bool isActive = false;
};
DEVEX_DECLARE_REFLECTION(Dialogue);

DEVEX_REFLECT(Dialogue)
{
    type.field("lines", &Dialogue::lines);
    type.field("currentLine", &Dialogue::currentLine);
    type.field("isActive", &Dialogue::isActive);
}

void startDialogue(devex::scene::Scene& scene, devex::scene::Entity entity,
                   const std::vector<std::string>& lines)
{
    auto* dialogue = scene.tryGet<Dialogue>(entity);
    if (dialogue == nullptr)
    {
        dialogue = &scene.add<Dialogue>(entity);
    }
    dialogue->lines = lines;
    dialogue->currentLine = 0;
    dialogue->isActive = !lines.empty();
}

void updateDialogue(devex::runtime::SystemContext& context)
{
    if (context.actions == nullptr || !context.actions->wasPressed("Jump"))
    {
        return;
    }
    for (auto [entity, dialogue] : context.scene.view<Dialogue>())
    {
        if (dialogue.isActive)
        {
            ++dialogue.currentLine;
            dialogue.isActive = dialogue.currentLine < dialogue.lines.size();
        }
    }
}

DEVEX_GAME_MODULE(game)
{
    game.component<Dialogue>();
    game.system("Dialogue", devex::runtime::SystemPhase::Update, &updateDialogue);
}
```

### 6.2. Exemples C#

#### 6.2.1. Système de Quête Simple

Ajouter `PlayerInventory` et `Quest` au joueur, et `Collectible` à un objet doté d'un collider
Trigger. L'inventaire garde l'identifiant de l'objet avant de le détruire : la quête peut donc se
terminer même lorsque l'entité ramassée n'existe plus.

<!-- devex-example: Quest.cs -->
```csharp
using System.Collections.Generic;
using Devex;

public class Quest : Component
{
    public string Title = "Quête";
    public string TargetItem = "key";
    public bool IsCompleted = false;
    public bool IsActive = true;

    public override void Update(float delta)
    {
        if (!IsActive || IsCompleted)
            return;

        var inventory = Entity.Get<PlayerInventory>();
        if (inventory != null && inventory.CollectedItems.Contains(TargetItem))
        {
            IsCompleted = true;
            Log.Info($"Quête '{Title}' terminée !");
        }
    }
}

public class PlayerInventory : Component
{
    public List<string> CollectedItems = new();

    public override void OnTriggerEnter(Entity other)
    {
        var collectible = other.Get<Collectible>();
        if (collectible != null)
        {
            if (!CollectedItems.Contains(collectible.ItemId))
                CollectedItems.Add(collectible.ItemId);
            other.Destroy();
        }
    }
}

public class Collectible : Component
{
    public string ItemId = "key";
}
```

#### 6.2.2. Système de Caméra Suiveuse

Ajouter ce script à une caméra à la racine, sans parent, et choisir sa cible dans l'inspecteur.
Le suivi a lieu dans `LateUpdate`, après l'interpolation de la physique. Le lissage tient compte
du temps écoulé, et la rotation regarde vers la cible selon la convention -Z du moteur.

<!-- devex-example: FollowCamera.cs -->
```csharp
using System;
using Devex;

public class FollowCamera : Component
{
    public Entity Target;
    public Vec3 Offset = new(0.0f, 2.0f, 5.0f);
    public float SmoothSpeed = 5.0f;

    public override void LateUpdate(float delta)
    {
        if (!Target.IsAlive || !Target.HasTransform)
            return;

        Vec3 target = Target.WorldPosition;
        Vec3 desired = target + Offset;
        float amount = 1.0f - MathF.Exp(-MathF.Max(0.0f, SmoothSpeed) * delta);
        Transform.Position += (desired - Transform.Position) * amount;

        Vec3 direction = target - Transform.Position;
        if (direction.Length > 0.0001f)
        {
            float yaw = MathF.Atan2(-direction.X, -direction.Z);
            float pitch = MathF.Atan2(direction.Y,
                MathF.Sqrt(direction.X * direction.X + direction.Z * direction.Z));
            Transform.Rotation = Quat.AngleAxis(yaw, Vec3.Up)
                * Quat.AngleAxis(pitch, Vec3.Right);
        }
    }
}
```

#### 6.2.3. Système de Santé et Dégâts

Les propriétés calculées n'ont pas besoin de `[Hidden]` : seuls les champs publics sont
sauvegardés et affichés par la réflexion. Un dégât négatif ne soigne pas le personnage, et une
santé maximale nulle donne un pourcentage nul.

<!-- devex-example: Health.cs -->
```csharp
using System;
using Devex;

public class Health : Component
{
    public float Current = 100.0f;
    public float Max = 100.0f;

    public bool IsAlive => Current > 0.0f;
    public float Percentage => Max > 0.0f ? Math.Clamp(Current / Max, 0.0f, 1.0f) : 0.0f;

    public void TakeDamage(float amount)
    {
        if (amount <= 0.0f || !IsAlive)
            return;

        Current = MathF.Max(0.0f, Current - amount);
        if (!IsAlive)
        {
            Log.Info($"{Entity.Name} est mort.");
            DestroyEntity();
        }
    }
}
```

---

## 7. Références Techniques

### 7.1. Types de Données et Conventions

#### 7.1.1. Types Primitifs

| Type C++ | Type C# | Taille | Description |
|----------|---------|--------|-------------|
| `bool` | `bool` | 1 byte | Booléen |
| `int32_t` | `int` | 4 bytes | Entier signé 32 bits |
| `uint32_t` | `uint` | 4 bytes | Entier non signé 32 bits |
| `float` | `float` | 4 bytes | Nombre à virgule flottante simple précision |
| `double` | `double` | 8 bytes | Nombre à virgule flottante double précision |
| `std::string` | `string` | Variable | Chaîne de caractères UTF-8 |

#### 7.1.2. Types Mathématiques

**Vecteurs :**
- `math::Vec2` / `Vec2` : Vecteur 2D (x, y) - 8 bytes
- `math::Vec3` / `Vec3` : Vecteur 3D (x, y, z) - 12 bytes
- `math::Vec4` / `Vec4` : Vecteur 4D (x, y, z, w) - 16 bytes

**Matrices :**
- `math::Mat3` : Matrice 3x3 - 36 bytes
- `math::Mat4` / `Mat4` : Matrice 4x4 - 64 bytes

**Quaternions :**
- `math::Quat` / `Quat` : Quaternion (x, y, z, w) - 16 bytes

**Autres :**
- `math::Aabb` : Boîte englobante alignée sur les axes
- `math::Trs` : Translation, Rotation, Scale
- `math::Extent2D` : Dimensions 2D (largeur, hauteur)

#### 7.1.3. Types de Composants Built-in

**Transformations et Rendu :**
- `Transform` : Position, rotation, scale relatif au parent
- `WorldTransform` : Transformation mondiale (calculée, non sauvegardée)
- `MeshRenderer` : Rendu de maillage 3D
- `SpriteRenderer` : Rendu de sprite 2D
- `Camera` : Caméra (3D ou orthographique)
- `DirectionalLight` : Lumière directionnelle (soleil)
- `PointLight` : Lumière ponctuelle
- `SpotLight` : Lumière directionnelle conique
- `Environment` : Environnement et ciel
- `ReflectionProbe` : Sonde de réflexion cuite dans l'éditeur (boîte, point de capture, fondu,
  correction de parallaxe, résolution, capture `.dvxprobe`)
- `LightProbeVolume` : Volume d'irradiance cuit dans l'éditeur (boîte, espacement des sondes,
  rebonds, fondu, lumière `.dvxirradiance` en harmoniques sphériques)

**Physique 3D :**
- `RigidBody` : Corps physique 3D
- `BoxCollider` : Collision boîte 3D
- `SphereCollider` : Collision sphère 3D
- `CapsuleCollider` : Collision capsule 3D
- `CylinderCollider` : Collision cylindre 3D
- `MeshCollider` : Collision maillage 3D
- `CharacterController` : Contrôleur de personnage 3D

**Physique 2D :**
- `RigidBody2D` : Corps physique 2D
- `BoxCollider2D` : Collision boîte 2D
- `CircleCollider2D` : Collision cercle 2D
- `CapsuleCollider2D` : Collision capsule 2D
- `PolygonCollider2D` : Collision polygone 2D (3-8 points)
- `TilemapCollider2D` : Collision tilemap 2D
- `CharacterController2D` : Contrôleur de personnage 2D

**Audio :**
- `AudioSource` : Source audio attachée à une entité
- `AudioListener` : Écouteur audio (généralement sur la caméra)

**Animation :**
- `Animator` : Lecteur d'animations
- `SpriteAnimator` : Lecteur d'animations de sprites

**UI :**
- Voir `UiComponents.hpp` pour la liste complète des composants UI

**Navigation :**
- `NavMeshAgent` : Agent de navigation

**Particules :**
- `ParticleEmitter` : Émetteur de particules

#### 7.1.4. Système de Réflexion

**ValueKind (Types supportés pour la réflexion) :**
- `Bool` : booléen
- `Int32` : entier 32 bits signé
- `UInt32` : entier 32 bits non signé
- `Float` : nombre flottant
- `String` : chaîne de caractères
- `Vec2` / `Vec3` / `Vec4` : vecteurs
- `Quat` : quaternion
- `Uuid` : identifiant unique
- `AssetId` : identifiant d'asset
- `Enum` : énumération
- `Entity` : référence à une entité

**Limitations :**
- Les listes de `bool` ne sont pas supportées (std::vector<bool> est spécial)
- Les types complexes doivent implémenter ValueTraits
- Les champs `Hidden` ne sont pas sauvegardés
- Les champs `Runtime` ne sont pas sauvegardés et non édités dans l'inspecteur

### 7.2. Conventions de Codage

#### 7.2.1. C++

**Nommage :**
- `PascalCase` pour les types et fonctions : `MyComponent`, `createEntity`
- `camelCase` pour les variables : `myVariable`, `currentPosition`
- `UPPER_CASE` pour les constantes : `MAX_SIZE`, `PI`
- Préfixe `m_` pour les membres privés : `m_components`, `m_name`
- Préfixe `s_` pour les variables statiques : `s_instanceCount`

**Style :**
- Indentation : 4 espaces
- Accolades sur la même ligne pour les fonctions courtes, nouvelle ligne pour les longues
- Espace avant les opérateurs binaires : `a + b`, `x * y`
- Pas d'espace à l'intérieur des parenthèses : `func(a, b)`
- Commentaires : `//` pour les commentaires de ligne, `/* */` pour les blocs

**Macros :**
- `DEVEX_API` : Export de symboles pour DLL
- `DEVEX_DECLARE_REFLECTION(Type)` : Déclaration de la réflexion
- `DEVEX_REFLECT(Type)` : Définition de la réflexion
- `DEVEX_DECLARE_ENGINE_REFLECTION(Type)` : Comme DECLARE mais pour le moteur
- `DEVEX_GAME_MODULE(name)` : Définition d'un module de jeu

#### 7.2.2. C#

**Nommage :**
- `PascalCase` pour les types, méthodes, propriétés : `MyClass`, `UpdateMethod`, `Speed`
- `camelCase` pour les variables locales et paramètres : `myVariable`, `deltaTime`
- `_camelCase` pour les champs privés : `_speed`, `_currentHealth`
- `UPPER_CASE` pour les constantes : `MAX_SPEED`, `DEFAULT_VALUE`

**Style :**
- Indentation : 4 espaces
- Accolades sur une nouvelle ligne
- Properties avec get/set sur une seule ligne si simple
- Utilisation de `var` lorsque le type est évident
- Commentaires XML pour la documentation publique

**Attributs :**
- `[Hidden]` : Champ non sauvegardé, non affiché dans l'inspecteur
- `[Angle]` : Champ float affiché en degrés mais stocké en radians
- `[Color]` : Vec3/Vec4 affiché comme couleur
- `[AssetType("type")]` : Limite AssetId à un type spécifique
- `[GameSystem(phase, Order = n)]` : Méthode statique utilisée comme système

### 7.3. Gestion de la Mémoire

#### 7.3.1. Allocation des Entités

- **Création** : `scene.createEntity()` alloue une nouvelle entité avec un UUID unique
- **Destruction** : `scene.destroyEntity()` libère l'entité et tous ses composants
- **Réutilisation** : Les slots d'entités libérés sont réutilisés (système de free list)

#### 7.3.2. Allocation des Composants

- Chaque type de composant a son propre `ComponentPool<T>`
- Les composants sont stockés dans des `std::vector<T>` contigus
- La suppression déplace le dernier élément pour remplir le vide (O(1))
- La mémoire n'est pas libérée immédiatement (le vector garde sa capacité)

#### 7.3.3. Gestion des Assets

- Les assets sont chargés par `AssetManager`
- Les assets actifs sont gardés en mémoire
- Les assets non utilisés peuvent être déchargés
- Les assets partagés (comme les textures utilisées par plusieurs matériaux) ne sont chargés qu'une fois

#### 7.3.4. Hot Reloading

**Pour les modules C++ :**
1. Détecter le changement du fichier DLL
2. Sauvegarder l'état des composants (via PreservedState)
3. Décharger la DLL
4. Recharger la nouvelle DLL
5. Recréer les composants avec l'état sauvegardé

**Pour les assemblies C# :**
1. Détecter le changement de l'assembly
2. Sauvegarder l'état des composants
3. Décharger le AssemblyLoadContext
4. Charger la nouvelle assembly dans un nouveau contexte
5. Recréer les composants avec l'état sauvegardé

**Limitations :**
- Seuls les champs primitifs et supportés sont préservés
- Les références à des types qui changent peuvent causer des problèmes
- Les composants C# doivent avoir un constructeur sans paramètres

---

## 8. Index des Classes et Fonctions

### 8.1. Index par Module C++

#### Scene Module
- `Scene` : Scène principale
- `Entity` : Identifiant d'entité
- `EntityRef` : Référence persistante à une entité
- `ComponentPool<T>` : Pool de composants
- `ComponentPoolBase` : Base des pools de composants
- `View<Components...>` : Vue sur les composants
- `ComponentRegistry` : Registre des types de composants

#### Components Module

Les 79 composants du moteur, par domaine (les jeux en ajoutent en C++ et en C#) :

- **3D** : `Transform` (et `WorldTransform`, calculée), `MeshRenderer`, `SkinnedMeshRenderer`,
  `Camera`, `DirectionalLight`, `PointLight`, `SpotLight`, `Environment`, `Atmosphere`, `Fog`,
  `PostProcessVolume`, `Terrain`, `ReflectionProbe`, `LightProbeVolume`
- **Physique 3D** : `RigidBody`, `BoxCollider`, `SphereCollider`, `CapsuleCollider`,
  `CylinderCollider`, `MeshCollider`, `CharacterController`, `HingeJoint`, `SliderJoint`,
  `DistanceJoint`, `FixedJoint`
- **2D** : `SpriteRenderer`, `SpriteAnimator`, `Tilemap`, `PointLight2D`, `DirectionalLight2D`,
  `LightOccluder2D`, `CanvasModulate`
- **Physique 2D** : `RigidBody2D`, `BoxCollider2D`, `CircleCollider2D`, `CapsuleCollider2D`,
  `PolygonCollider2D`, `TilemapCollider2D`, `CharacterController2D`, `HingeJoint2D`,
  `SliderJoint2D`, `DistanceJoint2D`, `FixedJoint2D`
- **Audio** : `AudioSource`, `AudioListener`
- **Animation** : `Animator`, `Tweener`
- **Particules** : `ParticleEmitter`, `TrailRenderer`
- **Navigation** : `NavMeshSurface`, `NavMeshAgent`, `NavMeshObstacle`
- **Interface** : `Canvas`, `UiRect`, `UiImage`, `UiText`, `UiButton`, `UiLayout`, `UiInput`,
  `UiScroll`, `UiSlider`, `UiToggle`, `UiBinding`, `UiPopup`, `UiContextMenu`, `UiTooltip`,
  `UiDropdown`, `UiSplitter`, `UiFoldout`, `UiVirtualList`, `UiTable`, `UiTableRow`,
  `UiDragSource`, `UiDropTarget`, `UiNumberField`, `UiColorPicker`, `UiPlot`, `UiLine`,
  `UiTextArea`

---

## 9. API C++ Complète - Modules Avancés

### 9.1. Assets et Gestion des Ressources

#### 9.1.1. AssetId (`devex::asset::AssetId`)

```cpp
// Dans: engine/include/devex/asset/AssetId.hpp

namespace devex::asset {
    // Identifie un asset indépendamment de son chemin, 
    // donc renommer ou déplacer le fichier garde les références valides
    struct DEVEX_API AssetId {
        core::Uuid uuid;
        
        [[nodiscard]] static AssetId generate();
        [[nodiscard]] constexpr bool isValid() const noexcept;
        constexpr auto operator<=>(const AssetId&) const noexcept = default;
    };
    
    // Assets fournis par le moteur lui-même
    namespace builtin {
        inline constexpr AssetId cubeMesh{core::Uuid::fromParts(0, 1)};
        inline constexpr AssetId sphereMesh{core::Uuid::fromParts(0, 2)};
        inline constexpr AssetId planeMesh{core::Uuid::fromParts(0, 3)};
    }
}
```

#### 9.1.2. Project (`devex::asset::Project`)

**Structure complète du projet :**

```cpp
// Dans: engine/include/devex/asset/Project.hpp

namespace devex::asset {
    // Configuration de la physique
    struct DEVEX_API PhysicsSettings {
        math::Vec3 gravity{0.0f, -9.81f, 0.0f};
        std::array<std::string, physicsLayerCount> layerNames{"Default"};
        std::array<std::uint16_t, physicsLayerCount> layerCollisions;
        
        [[nodiscard]] bool collides(std::uint32_t a, std::uint32_t b) const noexcept;
        void setCollides(std::uint32_t a, std::uint32_t b, bool collide) noexcept;
    };
    
    // Configuration audio
    struct DEVEX_API AudioSettings {
        float masterVolume = 1.0f;
        std::array<std::string, audioGroupCount> groupNames{"Effects", "Music", "Voice"};
        std::array<float, audioGroupCount> groupVolumes;
    };
    
    // Configuration des couches de tri (pour les sprites)
    struct DEVEX_API SortingSettings {
        std::vector<std::string> layers{"Default"};
        [[nodiscard]] std::int32_t rank(std::string_view layer) const noexcept;
    };
    
    // Configuration de la fenêtre
    struct DEVEX_API WindowSettings {
        std::uint32_t width = 1280;
        std::uint32_t height = 720;
        bool fullscreen = false;
        bool vsync = true;
        std::uint32_t maxFrameRate = 0;
        AssetId icon;
        bool desktopEntry = true;  // Linux : le jeu exporté s'inscrit parmi les applications
    };
    
    // Configuration d'export
    struct DEVEX_API ExportSettings {
        std::vector<std::string> scenes;
        std::vector<std::string> includeFolders;
        std::string output = "export/linux";  // "export/windows" sous Windows, "export/macos" sous macOS
        std::string configuration = "Release";
    };
    
    // Type d'action d'entrée
    enum class InputActionKind : std::uint8_t { Button, Axis, Vector };
    
    // Direction de l'entrée
    enum class InputDirection : std::uint8_t { Positive, Negative, Up, Down, Left, Right };
    
    // Liaison d'entrée
    struct DEVEX_API InputBinding {
        std::string input;  // "key:Space", "mouse:Left", "pad:South", etc.
        InputDirection direction = InputDirection::Positive;
    };
    
    // Action d'entrée
    struct DEVEX_API InputAction {
        std::string name;
        InputActionKind kind = InputActionKind::Button;
        std::string context;
        float deadZone = 0.0f;
        std::vector<InputBinding> bindings;
    };
    
    // Contexte d'entrée
    struct DEVEX_API InputContext {
        std::string name;
        bool activeAtStart = true;
    };
    
    // Configuration des entrées
    struct DEVEX_API InputSettings {
        std::vector<InputContext> contexts;
        std::vector<InputAction> actions;
    };
    
    // Projet complet
    struct DEVEX_API Project {
        std::string name;
        std::filesystem::path root;
        std::filesystem::path file;
        std::string startupScene;
        PhysicsSettings physics;
        AudioSettings audio;
        SortingSettings sorting;
        WindowSettings window;
        ExportSettings exportSettings;
        InputSettings input;
        
        [[nodiscard]] std::filesystem::path assetsDirectory() const;
        [[nodiscard]] std::filesystem::path cacheDirectory() const;
        [[nodiscard]] std::filesystem::path codeDirectory() const;
        [[nodiscard]] std::string resourcePath(const std::filesystem::path& path) const;
        [[nodiscard]] std::optional<std::filesystem::path> absolutePath(std::string_view resource) const;
    };
}
```

**Constantes importantes :**
- `physicsLayerCount = 16` : Nombre maximum de couches de collision
- `audioGroupCount = 8` : Nombre maximum de groupes audio
- `projectExtension = ".dvxproj"` : Extension des fichiers projet
- `resourceScheme = "res://"` : Schéma des ressources

#### 9.1.3. AssetManager (`devex::runtime::AssetManager`)

```cpp
// Dans: engine/include/devex/runtime/AssetManager.hpp

namespace devex::runtime {
    struct DEVEX_API LoadedFont {
        std::shared_ptr<const asset::FontData> data;
        render::TextureHandle atlas;
    };
    
    struct DEVEX_API LoadedMesh {
        render::MeshHandle handle;
        std::vector<asset::AssetId> submeshMaterials;
        std::size_t gpuBytes = 0;
    };
    
    class DEVEX_API AssetManager {
    public:
        // Constructeur : peut être null pour renderer, source, jobs
        AssetManager(render::Renderer* renderer, asset::AssetSource* source, core::JobSystem* jobs = nullptr) noexcept;
        ~AssetManager();
        
        // Méthodes de registration
        void registerMesh(asset::AssetId id, render::MeshHandle mesh,
                         std::vector<asset::AssetId> submeshMaterials = {});
        
        // Accès aux assets chargés
        [[nodiscard]] const LoadedMesh* mesh(asset::AssetId id);
        [[nodiscard]] render::MaterialHandle material(asset::AssetId id);
        [[nodiscard]] render::TextureHandle texture(asset::AssetId id);
        // Capture d'une sonde de réflexion, préfiltrée sur le GPU au premier appel
        [[nodiscard]] render::ReflectionProbeHandle reflectionProbe(asset::AssetId id);
        // Lumière d'un volume d'irradiance, copiée pour les shaders au premier appel
        [[nodiscard]] render::IrradianceVolumeHandle irradianceVolume(asset::AssetId id);
        [[nodiscard]] core::Result<void> setTexture(asset::AssetId id, const asset::TextureData& data);
        
        // Gestion du chargement
        void finishLoads();
        void preload(asset::AssetId id);
        [[nodiscard]] bool isReady(asset::AssetId id);
        [[nodiscard]] std::size_t pendingLoads() const noexcept;
        void waitForLoads();
        
        // Accès aux données CPU
        [[nodiscard]] const asset::ModelData* model(asset::AssetId id);
        [[nodiscard]] const asset::MeshData* meshData(asset::AssetId id);
        [[nodiscard]] core::Result<std::string> sceneText(asset::AssetId id);
        
        // Assets audio
        [[nodiscard]] std::shared_ptr<const audio::Clip> audioClip(asset::AssetId id);
        
        // Assets animation
        [[nodiscard]] std::shared_ptr<const animation::Clip> animationClip(asset::AssetId id);
        
        // Autres assets
        [[nodiscard]] math::Extent2D textureSize(asset::AssetId id) const noexcept;
        [[nodiscard]] std::shared_ptr<const asset::ThemeData> theme(asset::AssetId id);
        [[nodiscard]] std::shared_ptr<const asset::CurveData> curve(asset::AssetId id);
        [[nodiscard]] std::shared_ptr<const asset::SpriteData> sprite(asset::AssetId id);
        [[nodiscard]] std::shared_ptr<const asset::SpriteFramesData> spriteFrames(asset::AssetId id);
        [[nodiscard]] std::shared_ptr<const asset::TilesetData> tileset(asset::AssetId id);
        [[nodiscard]] std::shared_ptr<const asset::AnimatorData> animator(asset::AssetId id);
        [[nodiscard]] std::shared_ptr<const asset::NavMeshData> navMesh(asset::AssetId id);
        [[nodiscard]] const LoadedFont* font(asset::AssetId id);
        
        // Gestion des événements
        void handleEvents(std::span<const asset::AssetEvent> events);
        
        // Memory reporting
        [[nodiscard]] asset::MemoryReport memoryReport(std::size_t largest = 12) const;
        
        // Accesseurs
        [[nodiscard]] asset::AssetSource* source() const noexcept;
        void setSource(asset::AssetSource* source);
    };
}
```

**Fonctionnement :**
- **Chargement paresseux** : Les assets ne sont chargés que lorsqu'ils sont nécessaires
- **Chargement en arrière-plan** : Avec JobSystem, meshes et textures se chargent en ar

### 9.2. UI et Interface Utilisateur

#### 9.2.1. UiWorld (`devex::ui::UiWorld`)

**Aperçu complet de l'interface utilisateur :**

```cpp
// Dans: engine/include/devex/ui/UiWorld.hpp

namespace devex::ui {
    // Entrée UI
    struct DEVEX_API UiInput {
        math::Vec2 pointer{0.0f};
        bool pointerDown = false;
        bool pointerPressed = false;
        bool pointerReleased = false;
        bool pointerMoved = false;
        bool secondaryPressed = false;
        int moveX = 0;     // -1, 0, 1
        int moveY = 0;     // -1, 0, 1
        bool submitPressed = false;
        bool cancelPressed = false;
        float wheel = 0.0f;
        std::string typed;
        bool backspacePressed = false;
        bool deletePressed = false;
        bool leftPressed = false;
        bool rightPressed = false;
        bool upPressed = false;
        bool downPressed = false;
        bool homePressed = false;
        bool endPressed = false;
        bool selecting = false;
        bool copyPressed = false;
        bool cutPressed = false;
        bool pastePressed = false;
        bool selectAllPressed = false;
        std::string clipboard;
        bool pageUpPressed = false;
        bool pageDownPressed = false;
        bool tabPressed = false;
        bool undoPressed = false;
        bool redoPressed = false;
        bool wordModifier = false;
    };
    
    // Style des tooltips
    struct DEVEX_API TooltipStyle {
        math::Vec4 background{0.08f, 0.09f, 0.11f, 0.96f};
        math::Vec4 text{0.92f, 0.93f, 0.95f, 1.0f};
        asset::AssetId font;
        float size = 15.0f;
        float padding = 6.0f;
        float cornerRadius = 4.0f;
    };
    
    // Éléments transportés (Drag & Drop)
    struct DEVEX_API Carried {
        scene::Entity source;
        std::string type;
        std::string data;
        std::string label;
    };
    
    struct DEVEX_API Drop {
        scene::Entity source;
        scene::Entity target;
        std::string type;
        std::string data;
        math::Vec2 at{0.5f};  // Position dans le target (0-1)
    };
    
    // Monde UI principal
    class DEVEX_API UiWorld {
    public:
        // Layout d'un canvas
        struct DEVEX_API CanvasLayout {
            scene::Entity entity;
            LayoutResult layout;
            std::int32_t sortOrder = 0;
            bool interactive = true;
        };
        
        // Configuration
        void setFonts(std::function<FontRef(asset::AssetId)> fonts, asset::AssetId defaultFont = {});
        void setThemes(ThemeSource themes);
        void setTooltipStyle(TooltipStyle style);
        void setTooltipsDrawn(bool drawn) noexcept;
        
        // Mise à jour principale
        void update(scene::Scene& scene, math::Vec2 windowSize, const UiInput& input, core::Duration delta);
        void build(const scene::Scene& scene, const DrawContext& context, render::RenderWorld& world) const;
        
        // Gestion des popups
        void openPopup(scene::Scene& scene, scene::Entity popup, std::optional<math::Vec2> at = std::nullopt);
        void closePopup(scene::Scene& scene, scene::Entity popup);
        [[nodiscard]] bool isPopupOpen(const scene::Scene& scene, scene::Entity popup) const;
        
        // Gestion des champs de texte
        void startEditing(const scene::Scene& scene, scene::Entity field, bool selectAll = true);
        [[nodiscard]] const EditState* editStateOf(scene::Entity entity) const noexcept;
        
        // Gestion des zones de texte
        void setTextSpans(const scene::Scene& scene, scene::Entity area, std::vector<TextSpan> spans);
        void setTextHighlights(const scene::Scene& scene, scene::Entity area, std::vector<TextSpan> highlights);
        void setTextMarks(const scene::Scene& scene, scene::Entity area, std::vector<TextLineMark> marks);
        
        // Accès aux états
        [[nodiscard]] const Carried* carried() const noexcept;
        [[nodiscard]] scene::Entity dropTarget() const noexcept;
        [[nodiscard]] const Drop* dropped() const noexcept;
        [[nodiscard]] bool wasDropped(std::string_view action) const;
        [[nodiscard]] bool wasDropped(scene::Entity target) const;
        
        // Événements de clic
        [[nodiscard]] bool wasClicked(std::string_view action) const;
        [[nodiscard]] bool wasClicked(scene::Entity entity) const;
        [[nodiscard]] bool wasChanged(std::string_view action) const;
        [[nodiscard]] bool wasChanged(scene::Entity entity) const;
        [[nodiscard]] bool wasSubmitted(std::string_view action) const;
        [[nodiscard]] bool wasSubmitted(scene::Entity entity) const;
        [[nodiscard]] bool wasCancelled() const noexcept;
        [[nodiscard]] bool wasDoubleClicked(std::string_view action) const;
        [[nodiscard]] bool wasDoubleClicked(scene::Entity entity) const;
        
        // Focus et navigation
        [[nodiscard]] scene::Entity hovered() const noexcept;
        [[nodiscard]] scene::Entity focused() const noexcept;
        void setFocus(const scene::Scene& scene, scene::Entity entity);
        [[nodiscard]] bool pointerOverInterface() const noexcept;
        
        // Tooltip
        [[nodiscard]] std::optional<ShownTooltip> shownTooltip(const scene::Scene& scene) const;
        struct DEVEX_API ShownTooltip { std::string_view text; math::Vec2 at{0.0f}; };
        
        // Drag & Drop
        void carryFromOutside(std::string type, std::string data);
        
        // Nettoyage
        void clear();
        
        // Accès aux canvases
        [[nodiscard]] std::span<const CanvasLayout> canvases() const noexcept;
        [[nodiscard]] math::Vec4 tint(scene::Entity entity) const;
    };
}
```

**Fonctionnalités clés :**
- **Système de canvas** : Hiérarchie de canvases avec ordre de tri
- **Gestion des entrées** : Souris, clavier, navigation
- **Édition de texte** : Champs de texte, zones de texte multi-lignes
- **Drag & Drop** : Support complet avec types personnalisés
- **Tooltips** : Infobulles personnalisables
- **Popups et menus** : Fenêtres modales et contextuelles
- **Système de focus** : Gestion avancée du focus
- **Histogrammes et sélecteurs** : Contrôles spécialisés

#### 9.2.2. GlyphAtlas (`devex::ui::GlyphAtlas`)

Les lettres dessinées à la taille exacte où elles s'affichent, par FreeType, plutôt que tirées de
l'atlas de distances : plus nettes aux petites tailles des outils, avec leurs fûts sur des pixels
entiers. Un `DrawContext` qui porte un atlas (`glyphs`) dessine ainsi le texte simple (sans
contour, élément ni tourné ni mis à l'échelle) des polices qui gardent leur fichier
(`FontData::source`) ; le reste garde les distances.

```cpp
// Dans: engine/include/devex/ui/GlyphAtlas.hpp

namespace devex::ui {
    // Ce que les pages demandent au renderer (textures R8 de pageSize², lignes remplacées en place).
    struct DEVEX_API GlyphPages {
        std::function<render::TextureHandle(std::uint32_t size)> create;
        std::function<void(render::TextureHandle, std::uint32_t top, std::uint32_t height,
                           std::span<const std::byte> rows)> update;
        std::function<void(render::TextureHandle)> destroy;
    };
    // Pages d'un renderer (qui doit survivre à l'atlas), lues au pixel le plus proche.
    [[nodiscard]] DEVEX_API GlyphPages rendererPages(render::Renderer& renderer);

    class DEVEX_API GlyphAtlas {
    public:
        static constexpr std::uint32_t pageSize = 1024;
        static constexpr std::size_t maxPages = 8;   // au-delà, l'atlas repart de zéro
        static constexpr int subpixelSteps = 4;      // le stylo se pose au quart de pixel

        explicit GlyphAtlas(GlyphPages pages);
        // Image d'un caractère à une taille en pixels ; null si rien à dessiner.
        [[nodiscard]] const RasterGlyph* glyph(const asset::FontData& font, float pixels,
                                               std::uint32_t codepoint, int subpixel);
        void flush();   // envoie les lignes changées, avant la frame qui les montre
        void clear();
    };
    // Pixel entier à gauche du stylo et quart de pixel au-delà.
    [[nodiscard]] DEVEX_API SnappedPen snapPen(float x) noexcept;
}
```

**Utilisation :**
- L'éditeur (`EditorUiKit`) et l'application (interface du jeu) possèdent chacun un atlas et
  appellent `flush()` après avoir construit leurs listes de dessin ;
- les lots de lettres sont de type `UiDrawKind::Glyph` (la couverture lue dans le canal rouge de
  la page) ; `Renderer::updateTexture` remplace des lignes d'une texture non compressée sans
  recréer l'image ;
- hinting léger de FreeType (`FT_LOAD_TARGET_LIGHT`) : les lettres ne bougent que verticalement,
  les avances restent celles de la mise en page.

#### 9.2.3. Barres, ombres et focus

- `UiScroll` et `UiTextArea` dessinent leur pouce en capsule fine qui s'élargit sous le pointeur ;
  `DrawContext::scrollbars` (`ScrollbarLook{hover, held}`) dit au dessin où en est chaque barre,
  `UiWorld::build` le remplit. La molette et la piste font **glisser** l'offset vers sa cible
  (`UiWorld::glide`, `updateGlides`) : un test qui fait défiler laisse passer quelques frames.
- `UiImage` : `outline_color` / `outline_width` (bordure intérieure), `shadow_color` /
  `shadow_size` / `shadow_offset` (ombre douce, lots `UiDrawKind::Outline` et `Shadow`).
- `UiToggle` : cochée, la case se remplit de `check_color` et porte une coche de `mark_color`
  (`check_size` de la case, lot `UiDrawKind::Check`), animée par `DrawContext::toggles` ;
- `UiInput::focus_color` / `focus_width` : anneau du champ en cours d'édition ;
  `UiDropdown::list_shadow_color` / `list_shadow_size` ; `TooltipStyle::shadowColor` / `shadowSize`.
- `Renderer::requestWindowCapture()` : image de la fenêtre entière, interface comprise (réponse vide
  si la surface ne se relit pas) ; `ApplicationConfig::screenshot` et `devex-editor --screenshot`.
- `Atmosphere` (composant) : ciel physique de Hillaire, remplace celui de l'`Environment` ; `Fog` :
  brouillard de hauteur (`density`, `height`, `falloff`, `sky_blend`...) ;
  `DirectionalLight::followAtmosphere` teinte le soleil par l'air (`render::atmosphereTransmittance`
  sur CPU). Côté renderer : `RenderWorld::atmosphere` / `fog`, `AtmosphereRenderer` (tables et
  cubemaps, `shaders/atmosphere.slang`), `applyAtmosphere` dans `common.slang`.
- `Camera` : `depth_of_field`, `focus_distance`, `aperture` (nombre f), `auto_focus`,
  `motion_blur` (part d'obturation), `saturation`, `contrast`, `color_filter`, `temperature`,
  `tint`. `PostProcessVolume` (composant) : `global` ou boîte `size` + `blend_distance`, `weight`,
  `priority`, et pour chaque effet une case `override_*` suivie de ses réglages. Côté renderer :
  `RenderWorld::postVolumes` (`RenderPostVolume`, bits `PostOverride`), `render::blendPostVolumes`,
  `postVolumeInfluence`, `whiteBalanceFactors` (`devex/render/PostProcess.hpp`) ;
  `RenderCamera::lensEffects` (faux pour la caméra de l'éditeur) ; passes de `shaders/lens.slang`
  (liaisons 13 `postSource` et 14 `postChain[7]` du set de frame).
- Parité C#/C++ (jalon 84, table dans decisions.md) : C# `Scene.EntitiesWith<T>()` / `<T1, T2>()`,
  `Screen.SetSize/SetTitle/Fullscreen/Maximize/Restore/Center/DisplayScale`, `Terrains.HeightAt/
  NormalAt/IsHole/Raycast`, `Assets.NameOf/TypeOf`, `Curves.Evaluate`, `Meshes.Vertices/Indices/
  Bounds`, `Textures.Size`, `Sprites.Size/Pivot`, `Audio.ClipLength`, `Particles.Gravity`,
  `Physics.RaycastSolid` ; C++ `runtime::terrainHeightAt`, `terrainNormalAt`, `isTerrainHoleAt`,
  `raycastTerrain` (`devex/runtime/TerrainQueries.hpp`). Nouvelle fonction native : à la fin de
  `NativeApi` des deux côtés (ManagedGame.cpp, Interop.cs) et `bootstrapVersion` + 1.
- Culling sur GPU (jalon 85) : `VulkanRenderer::prepareCulling` (lots, `GpuInstance`, vues
  `GpuCullView`), `recordCulling` (passe *Culling* : copie des dessins, `shaders/cull.slang`,
  barrières), `drawCulled` (un `vkCmdDrawIndexedIndirect` par lot, `shaders/instanced.slang`) ;
  `isGpuDriven` dit ce qui reste au CPU (shader du projet, transparence, os sans compute) ; module
  `shaders/geometry.slang` partagé avec `mesh`, `prepass`, `shadow` et `pick`. Désactivable par
  `RendererConfig::gpuCulling` ; exige `drawIndirectFirstInstance`
  (`Device::supportsDrawIndirectFirstInstance`). Mémoire : `BufferConfig::hostReads` pour ce que le
  CPU relit, écriture séquentielle sinon. Mesure : `devex_render_tests "Renderer stress benchmark"`
  en Release (`DEVEX_GPU_CULLING=0` pour comparer).
- Occlusion GPU (jalon 87) : `Occlusion.cpp`, `shaders/depth_reduce.slang` et
  `shaders/occlusion.slang`. Deux prépasses : profondeur courante des instances présumées visibles,
  pyramide reverse-Z conservatrice, test de toutes les instances du frustum, puis récupération des
  instances redevenues visibles avant l'ombrage. L'ancien résultat, propre à chaque vue et contexte
  de frame, ne sert qu'à choisir la première prépasse ; ombres et picking restent indépendants.
  `RendererConfig::gpuOcclusion`, actif dès huit instances éligibles ; `RendererStats::gpuInstancesTested`,
  `gpuFrustumCulled`, `gpuOccludedInstances` et trois moniteurs dans Statistics (dernière frame
  terminée, sans attente ajoutée). API des jeux 30. Démo : `assets/scenes/occlusion.dvxscene` dans le
  sandbox ; régénération : `python scripts/generate_sample_assets.py --occlusion-only`. Benchmark :
  `DEVEX_GPU_OCCLUSION=0`, `DEVEX_STRESS_OCCLUDERS=1`, `DEVEX_PRESENT_MODE=immediate` et
  `DEVEX_STRESS_FRAMES` ; attentes de fence, acquisition et présentation imprimées séparément.
- Skinning compute (jalon 88) : `Skinning.cpp`, `shaders/skinning.slang`, `GpuSkinJob`,
  `GpuSkinnedVertex` (64 octets : positions homogènes actuelle/précédente, normale et tangente),
  `GpuSkinBounds` (boîte mondiale mesurée par réduction). Une pose compatible est partagée entre
  sous-maillages, puis entre prépasses, ombres, shading et picking ; buffers propres à chaque vue
  et contexte de frame. Animation des os toujours au CPU. `RendererConfig::gpuSkinning` (actif par
  défaut, exige `gpuCulling`), `RendererStats::gpuSkinnedPoses/gpuSkinnedVertices` (frame courante)
  et moniteurs Statistics ; API des jeux 31. Les shaders des projets, surfaces transparentes et
  masque de contour gardent leur skinning par dessin. Démo `assets/scenes/skinning.dvxscene` du
  sandbox (96 robots, `--skinning-only` dans le générateur) ; benchmark `DEVEX_STRESS_SKINNED=1`
  et `DEVEX_GPU_SKINNING=0` pour comparer, nombre de sommets et temps *Skinning* imprimés.
- Terrains : `asset::TerrainMaps` (`devex/asset/TerrainMaps.hpp` : `makeFlatTerrain`,
  `terrainHeight`, `terrainNormal`, `raycastTerrain`, `applyTerrainBrush`, `copyTerrainRegion`),
  `encodeTerrain`/`decodeTerrain`, asset `terrain` (`.dvxterrain`) ; composant `scene::Terrain`
  (`data`, `size`, `layers` de matériaux, `auto_*`, `grass_*`) ; `AssetManager::terrain` /
  `terrainChanged` / `terrainSaved` ; `RenderWorld::terrains` (`RenderTerrain`,
  `terrainBlockCells`), `shaders/terrain.slang` ; `PhysicsWorldConfig::terrains` ;
  `tools::TerrainAccess`, `TerrainPainter` et `TerrainPainter.cpp` dans l'éditeur.
- Journal : `LogRecord::category` (dossier du fichier, `core::logCategoryOf`, ou nommée par
  `DEVEX_LOG_INFO_IN("build", ...)` et ses voisins) ; `core::setLogLevel(catégorie, niveau)`,
  `core::configureLog("info,render=trace")`, `runtime::configureLogging` (variable `DEVEX_LOG` puis
  `--log-level` de l'éditeur et du lecteur) ; couleurs sur un terminal sauf `NO_COLOR`.
  `detail::buildLogCategory` pour les builds ; `GameCodeBuilder::stepsOf` lit « [n/m] » de Ninja.
- Ouverture : `GameCodeStatus::step` / `progress` / `lastLine` ; `ToolsState::scenesWaitForCode`
  retient les scènes tant que le code compile, dialogue `DialogKind::OpeningProject` (*Open Now*).
- `ApplicationConfig::profileReport` / `devex-editor --profile rapport.txt` : moyenne des zones CPU
  et des passes GPU sur 240 frames une fois la fenêtre au repos (`ToolsOverlay::setProfiling` garde
  le profileur allumé). `RendererStats::keptShadowPasses` : passes d'ombres gardées de la frame
  d'avant (cascades, atlas), redessinées seulement quand instances, os, matériaux ou vues changent.


### 9.3. Animation

#### 9.3.1. AnimationWorld (`devex::animation::AnimationWorld`)

```cpp
// Dans: engine/include/devex/animation/AnimationWorld.hpp

namespace devex::animation {
    // Statut d'un animateur
    struct DEVEX_API AnimatorStatus {
        struct DEVEX_API Parameter {
            std::string name;
            asset::AnimatorParameterType type = asset::AnimatorParameterType::Float;
            float value = 0.0f;  // Bools et triggers sont 0 ou 1
        };
        
        // Ce que fait une couche du contrôleur
        struct DEVEX_API Layer {
            std::string name;
            std::string state;
            float normalizedTime = 0.0f;     // Depuis le début de l'état
            std::string previousState;       // État dont il part en fondu
            float transitionProgress = 1.0f;
            float weight = 1.0f;             // Paramètre de poids compris ; 1 pour la base
        };

        bool playing = false;
        std::vector<Parameter> parameters;
        std::vector<Layer> layers;           // La couche de base d'abord
    };
    
    class DEVEX_API AnimationWorld {
    public:
        // Sources de données
        using ClipSource = std::function<std::shared_ptr<const Clip>(asset::AssetId clip)>;
        using AnimatorSource = std::function<std::shared_ptr<const asset::AnimatorData>(asset::AssetId controller)>;
        using SpriteFramesSource = std::function<std::shared_ptr<const asset::SpriteFramesData>(asset::AssetId frames)>;
        
        explicit AnimationWorld(ClipSource clips, AnimatorSource animators = {}, SpriteFramesSource spriteFrames = {});
        ~AnimationWorld();
        
        // Mise à jour principale
        void update(scene::Scene& scene, core::Duration delta);
        
        // Contrôle de la lecture
        void play(scene::Scene& scene, scene::Entity entity, asset::AssetId clip, float fade = -1.0f);
        void play(scene::Scene& scene, scene::Entity entity);
        void stop(scene::Entity entity);
        void pause(scene::Entity entity);
        void resume(scene::Entity entity);
        [[nodiscard]] bool isPlaying(scene::Entity entity) const;
        [[nodiscard]] float time(scene::Entity entity) const;
        void setTime(scene::Scene& scene, scene::Entity entity, float seconds);
        
        // Paramètres de l'animateur
        void setFloat(scene::Entity entity, std::string_view parameter, float value);
        void setInteger(scene::Entity entity, std::string_view parameter, std::int32_t value);
        void setBool(scene::Entity entity, std::string_view parameter, bool value);
        void setTrigger(scene::Entity entity, std::string_view parameter);
        void resetTrigger(scene::Entity entity, std::string_view parameter);
        [[nodiscard]] std::optional<float> parameter(scene::Entity entity, std::string_view parameter) const;
        // Sans nom de couche : la couche de base
        [[nodiscard]] std::string_view state(scene::Entity entity, std::string_view layer = {}) const;
        [[nodiscard]] float stateTime(scene::Entity entity, std::string_view layer = {}) const;
        // Poids d'une couche (0 à 1), à la place de celui du contrôleur
        void setLayerWeight(scene::Entity entity, std::string_view layer, float weight);
        [[nodiscard]] std::optional<float> layerWeight(scene::Entity entity, std::string_view layer) const;
        [[nodiscard]] std::optional<AnimatorStatus> status(scene::Entity entity) const;
        
        // Pause globale
        void setPaused(bool paused);
        [[nodiscard]] bool paused() const noexcept;
        
        // Statistiques
        [[nodiscard]] std::size_t animatorCount() const noexcept;
    };
    
    // Fonctions utilitaires
    DEVEX_API void applyClip(scene::Scene& scene, scene::Entity entity, const Clip& clip, float time);
    
    // Calcul des poids pour blend trees
    [[nodiscard]] DEVEX_API std::vector<float> linearBlendWeights(std::span<const float> thresholds, float value);
    [[nodiscard]] DEVEX_API std::vector<float> planarBlendWeights(std::span<const math::Vec2> positions, math::Vec2 value);
}
```

**Fonctionnalités :**
- **Animation par clips** : Système de clips d'animation
- **State machines** : Machines d'état avec transitions
- **Blend trees** : Arbres de mélange linéaires et planaires
- **Couches** : Machines à états par couche, en remplacement (Override) ou en ajout (Additive),
  avec masque d'os et poids (valeur, code, paramètre)
- **Pose de repos** : Les os qu'un clip du contrôleur anime reviennent au repos quand les états
  joués les laissent libres
- **Paramètres** : Contrôle via paramètres float, int, bool, trigger
- **Crossfading** : Transitions douces entre animations
- **Synchronisation** : Animation des os et des sprites

#### 9.3.2. Components d'Animation

**Composants built-in :**
- `Animator` : Lecteur d'animations avec state machine
- `SpriteAnimator` : Lecteur d'animations de sprites

**Types de paramètres et couches :**
```cpp
// Dans asset/AnimatorData.hpp
namespace devex::asset {
    enum class AnimatorParameterType : std::uint8_t {
        Float,
        Integer,
        Bool,
        Trigger
    };

    enum class AnimatorLayerBlend : std::uint8_t { Override, Additive };

    // Une machine à états du contrôleur ; la première est la couche de base
    struct DEVEX_API AnimatorLayer {
        std::string name = "Base Layer";
        std::string entry;
        std::vector<AnimatorState> states;
        std::vector<AnimatorTransition> transitions;
        AnimatorLayerBlend blending = AnimatorLayerBlend::Override;
        float weight = 1.0f;
        std::string weightParameter;          // Multiplie le poids
        std::vector<std::string> mask;        // Os mis dans le masque, avec ceux en dessous
        std::vector<std::string> maskExcluded; // Os laissés dehors, avec ceux en dessous
        math::Vec2 entryPosition, anyStatePosition;
    };

    struct DEVEX_API AnimatorData {
        std::vector<AnimatorParameter> parameters;
        std::vector<AnimatorLayer> layers;    // Jamais vide
    };
}
```

### 9.4. Navigation

#### 9.4.1. NavigationWorld (`devex::navigation::NavigationWorld`)

```cpp
// Dans: engine/include/devex/navigation/NavigationWorld.hpp

namespace devex::navigation {
    // Source de NavMesh
    using NavMeshSource = std::function<std::shared_ptr<const asset::NavMeshData>(asset::AssetId navMesh)>;
    
    struct DEVEX_API NavigationWorldConfig {
        NavMeshSource navMeshes;
        int maxAgents = 256;
        int maxObstacles = 256;
    };
    
    // Résultat de raycast sur NavMesh
    struct DEVEX_API NavHit {
        math::Vec3 position{0.0f};
        math::Vec3 normal{0.0f};
        float distance = 0.0f;
    };
    
    class DEVEX_API NavigationWorld {
    public:
        [[nodiscard]] static core::Result<std::unique_ptr<NavigationWorld>> create(NavigationWorldConfig config);
        ~NavigationWorld();
        
        // Mise à jour principale
        void update(scene::Scene& scene, core::Duration delta);
        
        // Contrôle des agents
        bool setDestination(scene::Entity agent, math::Vec3 destination);
        void stop(scene::Entity agent);
        [[nodiscard]] bool hasDestination(scene::Entity agent) const;
        [[nodiscard]] std::optional<math::Vec3> destination(scene::Entity agent) const;
        [[nodiscard]] float remainingDistance(scene::Entity agent) const;
        [[nodiscard]] std::vector<math::Vec3> path(scene::Entity agent) const;
        
        // Fonctions utilitaires
        [[nodiscard]] std::vector<math::Vec3> findPath(math::Vec3 from, math::Vec3 to) const;
        [[nodiscard]] std::optional<math::Vec3> samplePosition(math::Vec3 point, float maxDistance) const;
        [[nodiscard]] std::optional<NavHit> raycast(math::Vec3 from, math::Vec3 to) const;
        
        // Statistiques
        [[nodiscard]] bool hasNavMesh() const noexcept;
        [[nodiscard]] std::size_t agentCount() const noexcept;
        [[nodiscard]] std::size_t obstacleCount() const noexcept;
    };
}
```

**Fonctionnalités :**
- **Navigation Mesh** : NavMesh cuit pour le pathfinding
- **Agents** : Entités qui se déplacent sur le NavMesh
- **Obstacles** : Obstacles dynamiques découpés du NavMesh
- **Pathfinding** : Trouver le chemin le plus court
- **Évitement de foule** : Système de crowd steering
- **Raycasting** : Détection d'obstacles sur le NavMesh

#### 9.4.2. Components de Navigation

**Composants built-in :**
- `NavMeshSurface` : Surface avec NavMesh
- `NavMeshAgent` : Agent de navigation
- `NavMeshObstacle` : Obstacle dynamique

#### 9.4.3. Cuire les terrains

Dans l'inspecteur d'une `NavMeshSurface`, **Bake** rassemble les colliders statiques et les
`Terrain` dont `collision` est activé. Il lit les cartes de terrain courantes, y compris la
sculpture et les trous pas encore enregistrés. Après un changement du terrain, recuire le
maillage avant de jouer ; les agents C++ et C# l'utilisent ensuite de la même manière.

En C++, la surcharge `navigation::bakeNavMesh(scene, meshes, terrains, settings)` reçoit une
`MeshSource` et une `TerrainSource` ; chacune renvoie les données d'un asset, ou `nullptr` si
elles ne sont pas disponibles. Appeler `scene.updateTransforms()` avant la cuisson et garder
les cartes valides jusqu'à son retour. La surcharge prenant une `NavGeometry` reste disponible
pour les triangles construits par le jeu.

Comme pour la physique, le terrain part de sa position mondiale vers +X et +Z sur `Terrain.size` ;
sa rotation et son échelle ne changent pas ses hauteurs. Les trous et la diagonale des triangles
suivent Jolt. La pente maximale et le rayon de l'agent retirent les zones où il ne peut pas passer.
La cuisson ne développe que les lignes du terrain touchant la tuile en cours, sans créer un
maillage de triangles de toute la carte en mémoire.

Les couches compressées gardent le relief ; au chargement et après le déplacement d'un obstacle,
le détail des hauteurs est reconstruit pour les requêtes, les agents et le dessin dans la vue.
Le format `.dvxnavmesh` reste le même. Dans le sandbox, **Hill robot**, à l'est de l'arène,
patrouille depuis le bas de la colline jusqu'à environ huit mètres de hauteur.


### 9.5. Particules

#### 9.5.1. ParticleWorld (`devex::particles::ParticleWorld`)

```cpp
// Dans: engine/include/devex/particles/ParticleWorld.hpp

namespace devex::particles {
    // Source d'émetteurs de particules
    using EmitterSource = std::function<std::shared_ptr<const ParticleEmitter>(asset::AssetId emitter)>;
    
    struct DEVEX_API ParticleWorldConfig {
        EmitterSource emitters;
    };
    
    class DEVEX_API ParticleWorld {
    public:
        [[nodiscard]] static core::Result<std::unique_ptr<ParticleWorld>> create(ParticleWorldConfig config);
        ~ParticleWorld();
        
        // Mise à jour principale
        void update(scene::Scene& scene, core::Duration delta);
        
        // Contrôle des émetteurs
        void play(scene::Entity entity);
        void stop(scene::Entity entity);
        void burst(scene::Entity entity, std::size_t count);
        
        // Statistiques
        [[nodiscard]] std::size_t emitterCount() const noexcept;
        [[nodiscard]] std::size_t particleCount() const noexcept;
    };
}
```

**Fonctionnalités :**
- **Émetteurs de particules** : Système d'émetteurs configurables
- **Types d'émetteurs** : Points, lignes, cercles, sphères, etc.
- **Contrôle du flux** : Émission continue ou en rafale
- **Durée de vie** : Particules avec durée de vie variable
- **Effets visuels** : Taille, couleur, vitesse variables dans le temps

#### 9.5.2. ParticleEmitter Components

**Composants built-in :**
- `ParticleEmitter` : Émetteur de particules configuré

### 9.6. Audio

#### 9.6.1. AudioWorld (`devex::audio::AudioWorld`)

```cpp
// Dans: engine/include/devex/audio/AudioWorld.hpp

namespace devex::audio {
    class DEVEX_API AudioWorld {
    public:
        using ClipSource = std::function<std::shared_ptr<const Clip>(asset::AssetId clip)>;
        
        AudioWorld(AudioEngine& engine, ClipSource clips);
        ~AudioWorld();
        
        // Mise à jour principale
        void update(scene::Scene& scene, core::Duration delta);
        
        // Contrôle des sources audio
        void play(scene::Scene& scene, scene::Entity entity);
        void stop(scene::Entity entity);
        void pause(scene::Entity entity);
        void resume(scene::Entity entity);
        [[nodiscard]] bool isPlaying(scene::Entity entity) const;
        
        // One-shot sounds
        void playOneShot(asset::AssetId clip, math::Vec3 position, float volume = 1.0f, 
                       std::uint32_t group = 0, bool spatial = true);
        
        // Contrôle global
        void setPaused(bool paused);
        [[nodiscard]] bool paused() const noexcept;
        [[nodiscard]] std::size_t soundCount() const noexcept;
        
        // Accès au moteur
        [[nodiscard]] AudioEngine& engine() noexcept;
    };
}
```

**Fonctionnalités :**
- **Audio 3D** : Positionnement spatial du son
- **Groupes audio** : Contrôle du volume par groupe
- **Audio sources** : Sources attachées aux entités
- **One-shot sounds** : Sons joués une fois
- **Effets** : Réverbération, occlusion, etc.

#### 9.6.2. Audio Components

**Composants built-in :**
- `AudioSource` : Source audio avec clip, volume, pitch, loop
- `AudioListener` : Écouteur audio (généralement sur la caméra)

### 9.7. Rendu Vulkan

#### 9.7.1. Architecture de Rendu

Le renderer (`devex/render/Renderer.hpp`, implémenté par `VulkanRenderer`) dessine un `RenderWorld`
que le runtime remplit à chaque frame. Vulkan 1.4, chargé par volk, mémoire par VMA, shaders Slang :

- **Render graph** : chaque frame déclare ses passes et les images qu'elles lisent et écrivent ; le
  graphe pose les barrières des images et mesure chaque passe par des timestamps GPU (panneau
  Profiler, `devex-editor --profile`).
- **Sans état de sommets** : les shaders lisent sommets, matériaux et données de scène par les
  adresses des buffers (vertex pulling) ; les textures sont dans un tableau sans liaison fixe
  (descriptor indexing). Deux frames en vol, chacune avec ses propres buffers.
- **Forward+ en clusters** avec une prépasse (profondeur, mouvement, normales), occlusion
  ambiante, anticrénelage temporel, bloom, profondeur de champ, flou de mouvement, étalonnage et
  tonemapping (AgX, PBR Neutral, ACES), mélangés par les `PostProcessVolume`.
- **Culling sur le GPU** (jalon 85) : une passe compute (`shaders/cull.slang`) teste les instances
  des matériaux standard dans la caméra, les cascades, les vues des lumières locales et le
  picking ; chaque passe les dessine par lots (même sous-maillage, même matériau) en dessin
  indirect (`shaders/instanced.slang`). Les maillages animés standard rejoignent ce chemin au
  jalon 88, leur pose et leurs limites préparées une fois par vue en compute. Shaders des projets
  et transparence restent cullés et dessinés par le CPU. Mesures : `docs/performances.md`.
- **Occlusion GPU** (jalon 87) : profondeur courante réduite en pyramide, puis récupération des
  instances redevenues visibles dans la même frame. Ombres et picking indépendants. Trois
  compteurs dans Statistics ; `RendererConfig::gpuOcclusion` permet de la désactiver.
- **Ombres** : quatre cascades pour le soleil et un atlas de 4096² pour les lumières locales,
  gardés d'une frame à l'autre tant que ce qui les dessine ne change pas.
- **Ciel et lumière** : atmosphère physique (`AtmosphereRenderer`), brouillard de hauteur, ciel HDR
  préfiltré (`EnvironmentBaker`), sondes de réflexion et volumes d'irradiance cuits.
- **Terrains** : blocs à niveau de détail continu et herbe (`shaders/terrain.slang`).
- **Mémoire** : les buffers écrits par le CPU chaque frame vont en mémoire vidéo quand la carte le
  permet ; ceux que le CPU relit gardent une mémoire en cache (`BufferConfig::hostReads`).

**Classes principales :**
- `Renderer` : Rendeur principal
- `VulkanRenderer` : Implémentation Vulkan
- `RenderWorld` : Monde de rendu avec toutes les entités visibles (dont `reflectionProbes`, et
  les `views` dont les vues `radiance` capturent la lumière en nits pour la cuisson des sondes)
- `EnvironmentBaker` : Préfiltrage GGX du ciel et des sondes de réflexion (`bakeProbe`)
- `AtmosphereRenderer` : Tables de l'atmosphère et lumière du ciel
- `CubeMaps` : Orientation des faces de cube (`cubeFaceView`, `cubeFaceDirection`)
- `SphericalHarmonics` : Éclairement d'ordre 2 projeté d'un cube et évalué pour une normale
  (`projectIrradiance`, `evaluateIrradiance`)
- `Frustum` (`devex/render/Culling.hpp`) : Tronc de vue et test des boîtes, sur le CPU
- `RenderGraph` : Graphique de passe de rendu
- `Pipeline` : Pipeline graphique ou compute
- `Device` : Appareil Vulkan
- `Instance` : Instance Vulkan
- `Swapchain` : Chaîne d'échange
- `Commands` : Gestion des commandes
- `Memory` : Gestion de la mémoire GPU (`Allocator`, `Buffer`, `Image`)
- `DescriptorSets` : Jeu de descripteurs

---

## 10. API C# Complète - Services et Systèmes Avancés

Les déclarations ci-dessous sont tirées des sources de `managed/Devex.Managed` (corps des méthodes
retirés, commentaires gardés) : elles disent exactement ce que le C# d'un jeu peut appeler. Le
tableau *Parité du C# et du C++* de `docs/decisions.md` dit ce qui correspond à chacune côté C++.

### 10.1. Services (`Devex`, `Services.cs`)

Le jeu, le temps, la fenêtre, les assets et leurs données, les terrains, les préfabs, la physique 3D
et 2D, la navigation, l'audio, les matériaux, l'animation, les tilemaps, les particules et
l'interface.

```csharp
// The game as a whole: quitting it and changing its scene.
public static unsafe class Game
{
    // Ends the game: the player quits, and the editor stops playing.
    public static void Quit();
    // Replaces the scene once the updates of the frame are done; the new scene starts over.
    public static void LoadScene(AssetId scene);
    // Replaces the scene by its path, such as "res://assets/scenes/arena.dvxscene".
    public static void LoadScene(string path);
    // Loads a scene in the background: what it shows loads while this scene goes on, then it replaces
    // this one. LoadingProgress says how far it is, for a loading screen.
    public static void LoadSceneInBackground(AssetId scene);
    // Loads a scene in the background by its path.
    public static void LoadSceneInBackground(string path);
    // Whether a scene is loading in the background.
    public static bool IsLoadingScene { get; }
    // How much of the scene loading in the background is ready, from 0 to 1; 0 when none is.
    public static float LoadingProgress { get; }
}

// Time of the game.
public static class Time
{
    // Seconds since the previous frame during Update, the fixed step during FixedUpdate.
    public static float Delta { get; internal set; }
    // Seconds of game time since the scene started.
    public static double Elapsed { get; internal set; }
    // Frames since the scene started.
    public static long Frame { get; internal set; }
}

// The window the game draws in.
public static unsafe class Screen
{
    // Its size in pixels.
    public static Vec2 Size { get; }
    // Resizes the window, in the coordinates of the window (which differ from pixels on a display
    // scaled by the system), first putting it back to its normal state.
    public static void SetSize(int width, int height);
    // The words in the title bar of the window.
    public static void SetTitle(string title);
    // Borderless over the whole display. PlayerSettings.Fullscreen does the same and keeps the choice
    // of the player for the next time the game starts.
    public static bool Fullscreen { get; set; }
    public static bool IsMaximized { get; }
    public static bool IsMinimized { get; }
    // The scale the system gives the display of the window: 1.5 at 150 %.
    public static float DisplayScale { get; }
    public static void Maximize();
    // Leaves the maximized or minimized state.
    public static void Restore();
    // Centers the window on its display.
    public static void Center();
}

// The assets of the game, which fields refer to by AssetId.
public static unsafe class Assets
{
    // The asset of a source file, such as "res://assets/prefabs/crate.dvxscene"; null when there is none.
    // An exported game only has the assets its scenes need and those of the folders the project always
    // exports: assets found by path belong in those folders, or in a field of a component.
    public static AssetId? Find(string path);
    // Starts loading an asset without using it, so that it is ready when it is shown: meshes, textures
    // and materials load in the background.
    public static void Preload(AssetId asset);
    // Whether the asset is loaded and, for what is drawn, on the GPU.
    public static bool IsReady(AssetId asset);
    // The name of the asset, as FileSystem shows it; null for an asset the game does not have.
    public static string? NameOf(AssetId asset);
    // The type of the asset: "mesh", "texture", "material", "scene", "audio"...; null for none.
    public static string? TypeOf(AssetId asset);
}

// The curves drawn by hand in the editor, read as tweens read them.
public static unsafe class Curves
{
    // The value of the curve at a time; 0 for a curve that cannot be loaded.
    public static float Evaluate(AssetId curve, float time);
}

public struct MeshVertex
{
    public Vec3 Position;
    public Vec3 Normal;
    // Texture coordinates, with the origin at the top-left corner.
    public Vec2 Uv;
    // The direction of increasing U; W is the sign of the bitangent.
    public Vec4 Tangent;
}

// The triangles of meshes, read as mesh colliders and the navigation read them.
public static unsafe class Meshes
{
    // The vertices of a mesh, copied; empty when it cannot be loaded.
    public static MeshVertex[] Vertices(AssetId mesh);
    // The indices of its triangles, three by three, copied.
    public static uint[] Indices(AssetId mesh);
    // The box around its vertices, in the space of the mesh; false when it cannot be loaded.
    public static bool Bounds(AssetId mesh, out Vec3 min, out Vec3 max);
}

// The images of the game.
public static unsafe class Textures
{
    // Its width and height in pixels, once it is loaded; zero before.
    public static Vec2 Size(AssetId texture);
}

// The sprites cut from textures.
public static unsafe class Sprites
{
    // Its width and height in meters, as it is drawn; zero for a sprite that cannot be loaded.
    public static Vec2 Size(AssetId sprite);
    // The point it stands on, from (0, 0) at its bottom-left corner to (1, 1) at its top-right one.
    public static Vec2 Pivot(AssetId sprite);
}

public struct TerrainHit
{
    public Vec3 Point;
    public Vec3 Normal;
    public float Distance;
}

// The ground of the terrains of the scene: its height under a point, which way it faces, its holes,
// and where a ray meets it. Without a terrain entity, every terrain of the scene is looked at.
public static unsafe class Terrains
{
    // The height in the world of the ground above or below a point; false outside it, or over a hole.
    public static bool HeightAt(Vec3 point, out float height, Entity terrain = default);
    // Which way the ground faces there.
    public static bool NormalAt(Vec3 point, out Vec3 normal, Entity terrain = default);
    // Whether the ground has a hole there, which nothing stands on.
    public static bool IsHole(Vec3 point, Entity terrain = default);
    // Where a ray first meets the ground within the distance, passing through its holes.
    public static bool Raycast(Vec3 origin, Vec3 direction, float maxDistance, out TerrainHit hit, Entity terrain = default);
}

// Prefabs placed by the game while it plays.
public static unsafe class Prefabs
{
    // A new instance of the prefab, under a parent or at the root; None when it cannot be loaded.
    public static Entity Instantiate(AssetId prefab, Entity parent = default);
    // A new instance of the prefab, placed at a position of its parent (of the world at the root).
    public static Entity Instantiate(AssetId prefab, Vec3 position, Entity parent = default);
}

public struct RayHit
{
    // The entity of the body hit: the entity of its RigidBody, or of its collider.
    public Entity Entity;
    public Vec3 Point;
    public Vec3 Normal;
    public float Distance;
}

// Whether two bodies started or stopped touching.
public enum ContactPhase
{
    Begin = 0,
    End = 1,
}

public readonly struct Contact
{
    public bool Involves(Entity entity);
    // The entity touching the given one.
    public Entity Other(Entity entity);
}

public readonly struct JointBreak
{
    public readonly Entity Joint;
    public readonly Entity BodyA;
    public readonly Entity BodyB;
}

// The simulation of the scene: queries, forces, and the contacts of the frame.
public static unsafe class Physics
{
    // A mask of every collision layer.
    public const uint AllLayers = 0xFFFF;
    // The closest body along a ray, among the layers of the mask, ignoring the bodies of an entity.
    public static bool Raycast(Vec3 origin, Vec3 direction, float maxDistance, out RayHit hit, uint layers = AllLayers, Entity ignore = default);
    // The first body a sphere moving along the direction touches.
    public static bool SphereCast(Vec3 origin, float radius, Vec3 direction, float maxDistance, out RayHit hit, uint layers = AllLayers, Entity ignore = default);
    // The closest solid body along a ray, passing through triggers, as particles bounce off what they meet.
    public static bool RaycastSolid(Vec3 origin, Vec3 direction, float maxDistance, out RayHit hit, uint layers = AllLayers);
    // The entities whose bodies intersect a sphere, each once.
    public static Entity[] OverlapSphere(Vec3 center, float radius, uint layers = AllLayers, Entity ignore = default);
    // A force on the dynamic body of the entity during the next step, in world space.
    public static void AddForce(Entity entity, Vec3 force);
    public static void AddTorque(Entity entity, Vec3 torque);
    // Changes the velocity of the dynamic body of the entity at once.
    public static void AddImpulse(Entity entity, Vec3 impulse);
    public static void AddImpulseAt(Entity entity, Vec3 impulse, Vec3 point);
    // The contacts that began or ended during the physics steps of the frame, seen during Update: those
    // of 3D bodies, then those of 2D bodies.
    public static ReadOnlySpan<Contact> Contacts { get; }
    // The joints, of 3D and 2D physics, that broke past their force or torque during the physics steps
    // of the frame, seen during Update. A broken joint stays broken until its component changes.
    public static ReadOnlySpan<JointBreak> BrokenJoints { get; }
}

// The navigation of the scene: NavMeshAgent components walk the navigation mesh of its
// NavMeshSurface to the destinations given here, and paths along it are found on demand.
public static unsafe class Navigation
{
    // Sends an agent to the point of the navigation mesh closest to a destination; false when the
    // destination is far from the mesh.
    public static bool SetDestination(Entity agent, Vec3 destination);
    // Stops the agent where it is.
    public static void Stop(Entity agent);
    // Whether the agent walks to a destination; false once it arrived.
    public static bool HasDestination(Entity agent);
    // How far the agent still has to walk along its path.
    public static float RemainingDistance(Entity agent);
    // The corners of the shortest path along the navigation mesh; empty when there is none.
    public static Vec3[] FindPath(Vec3 from, Vec3 to);
    // The point of the navigation mesh closest to a point, within a distance.
    public static bool SamplePosition(Vec3 point, float maxDistance, out Vec3 position);
    // Walks a straight line along the navigation mesh; true when it leaves the mesh, at the position given.
    public static bool Raycast(Vec3 from, Vec3 to, out Vec3 position, out Vec3 normal);
}

public struct RayHit2D
{
    // The entity of the body hit: the entity of its RigidBody2D, or of its collider.
    public Entity Entity;
    public Vec2 Point;
    public Vec2 Normal;
    public float Distance;
}

// The 2D simulation of the scene, in the XY plane: queries and forces. Its contacts come with the
// others in Physics.Contacts, and reach OnCollisionEnter and OnTriggerEnter alike.
public static unsafe class Physics2D
{
    // The closest 2D body along a ray, among the layers of the mask, through triggers, ignoring the
    // bodies of an entity.
    public static bool Raycast(Vec2 origin, Vec2 direction, float maxDistance, out RayHit2D hit, uint layers = Physics.AllLayers, Entity ignore = default);
    // The entities whose 2D bodies, triggers included, overlap a circle, each once.
    public static Entity[] OverlapCircle(Vec2 center, float radius, uint layers = Physics.AllLayers, Entity ignore = default);
    // A force on the dynamic 2D body of the entity during the next step.
    public static void AddForce(Entity entity, Vec2 force);
    // A torque around Z on the dynamic 2D body of the entity during the next step.
    public static void AddTorque(Entity entity, float torque);
    // Changes the velocity of the dynamic 2D body of the entity at once.
    public static void AddImpulse(Entity entity, Vec2 impulse);
}

// The sounds of the game: those of the AudioSource components of entities, sounds played once, and
// the volumes of the audio groups of the project.
public static unsafe class Audio
{
    // The name of the volume of every group together.
    public const string Master = "Master";
    // Plays the clip of the AudioSource of the entity from its start.
    public static void Play(Entity entity);
    public static void Stop(Entity entity);
    public static void Pause(Entity entity);
    // Goes on from where Pause stopped.
    public static void Resume(Entity entity);
    public static bool IsPlaying(Entity entity);
    // The length of a clip in seconds; 0 for a clip that cannot be loaded.
    public static double ClipLength(AssetId clip);
    // Plays a clip once at a position of the world, in an audio group.
    public static void PlayOneShot(AssetId clip, Vec3 position, float volume = 1.0f, uint group = 0);
    // Plays a clip once, everywhere at the same volume, as music or the sounds of an interface.
    public static void PlayOneShot(AssetId clip, float volume = 1.0f, uint group = 0);
    // The volume of an audio group of the project, or of all of them with Master.
    public static float GetGroupVolume(string group);
    // Changes the volume of an audio group until the game ends, as a settings menu does: 1 leaves the
    // sounds as they are, 0 silences them.
    public static void SetGroupVolume(string group, float volume);
}

// The uniforms of the materials drawn by shaders of the project, changed while the game runs, as
// Godot's set_shader_parameter: every object drawn with the material changes. Its file keeps its
// values, which come back when the editor stops playing. Instance uniforms are given by each object,
// with InstanceShaderParameters.
public static unsafe class Materials
{
    public static void SetShaderParameter(AssetId material, string name, float value);
    public static void SetShaderParameter(AssetId material, string name, Vec2 value);
    public static void SetShaderParameter(AssetId material, string name, Vec3 value);
    public static void SetShaderParameter(AssetId material, string name, Vec4 value);
    public static void SetShaderParameter(AssetId material, string name, int value);
    public static void SetShaderParameter(AssetId material, string name, bool value);
    // Gives a texture uniform of the material another texture of the project.
    public static void SetShaderParameter(AssetId material, string name, AssetId texture);
    // The value of a uniform of the material, in as many components as it has: the one it was given,
    // else the default of its shader.
    public static Vec4 GetShaderParameter(AssetId material, string name);
}

// The instance uniforms of the shader an object draws with, as Godot's set_instance_shader_parameter:
// its renderer (MeshRenderer, SkinnedMeshRenderer, SpriteRenderer, Tilemap) gives them its own values,
// the flash of one enemy that was hit, and the objects that give none keep the defaults of the
// shader. Tweens reach them as "SpriteRenderer.instance_shader_parameters/flash".
public static unsafe class InstanceShaderParameters
{
    public static void SetInstanceShaderParameter(this Entity entity, string name, float value);
    public static void SetInstanceShaderParameter(this Entity entity, string name, Vec2 value);
    public static void SetInstanceShaderParameter(this Entity entity, string name, Vec3 value);
    public static void SetInstanceShaderParameter(this Entity entity, string name, Vec4 value);
    public static void SetInstanceShaderParameter(this Entity entity, string name, int value);
    public static void SetInstanceShaderParameter(this Entity entity, string name, bool value);
    // The value the object gives the uniform, else the default of its shader.
    public static Vec4 GetInstanceShaderParameter(this Entity entity, string name);
    // Lets the object take the default of the shader again.
    public static void ResetInstanceShaderParameter(this Entity entity, string name);
}

// The animations of the game: the clips the Animator components play on the bones of their entity.
// An event an animation played past: the entity of its Animator or SpriteAnimator, and the name the animation gives it.
public readonly record struct AnimationEvent(Entity Entity, string Name);
{
    // The events the animations played past this frame, in the order they came: those of the clips
    // of Animators, set in the import settings of their model, and of the frames of SpriteAnimators,
    // set in their .dvxframes. They also reach the OnAnimationEvent of the components of the entity.
    public static AnimationEvent[] Events { get; }
    // Plays a clip on the animator of the entity, crossfading over fade seconds; a negative fade
    // takes the blend time of the Animator. The clip becomes the one the Animator holds.
    public static void Play(Entity entity, AssetId clip, float fade = -1.0f);
    // Plays the clip the Animator already holds, from its start.
    public static void Play(Entity entity);
    public static void Stop(Entity entity);
    public static void Pause(Entity entity);
    // Goes on from where Pause stopped.
    public static void Resume(Entity entity);
    public static bool IsPlaying(Entity entity);
    // Seconds into the clip that plays.
    public static float GetTime(Entity entity);
    // Jumps to a time of the clip and poses the bones there.
    public static void SetTime(Entity entity, float seconds);
    // Sets a float parameter of the state machine of the Animator of the entity.
    public static void SetFloat(Entity entity, string parameter, float value);
    public static void SetInteger(Entity entity, string parameter, int value);
    public static void SetBool(Entity entity, string parameter, bool value);
    // Sets a trigger, which the transition it lets through resets.
    public static void SetTrigger(Entity entity, string parameter);
    public static void ResetTrigger(Entity entity, string parameter);
    // A parameter of the state machine; bools and triggers read as 0 or 1.
    public static float GetFloat(Entity entity, string parameter);
    public static int GetInteger(Entity entity, string parameter);
    public static bool GetBool(Entity entity, string parameter);
    // The state the state machine of the entity is in; empty without a controller.
    public static string GetState(Entity entity);
    // The state a layer of the state machine is in; empty without that layer.
    public static string GetState(Entity entity, string layer);
    // Whether the state machine of the entity is in a state.
    public static bool IsInState(Entity entity, string state);
    // Whether a layer of the state machine of the entity is in a state.
    public static bool IsInState(Entity entity, string state, string layer);
    // How far into its state the state machine is: 1 at its end, more for a state that loops.
    public static float GetStateTime(Entity entity);
    // How far into its state a layer of the state machine is.
    public static float GetStateTime(Entity entity, string layer);
    // Sets how much of the pose of a layer of the state machine shows, from 0 to 1, in place of the
    // weight its controller gives it: an arm that aims only while the character holds a weapon. The
    // weight parameter of the layer, when it names one, still multiplies it.
    public static void SetLayerWeight(Entity entity, string layer, float weight);
    // The weight code or the controller gives a layer; 0 for a layer neither knows.
    public static float GetLayerWeight(Entity entity, string layer);
}

// Frame by frame animation of sprites: the SpriteAnimator components, which show the frames of an
// animation of their sprite frames on the SpriteRenderer of their entity.
public static class SpriteAnimations
{
    // Plays an animation by name. Another animation starts from its first frame; the one already
    // playing goes on, so that this can be called every frame; one that ended starts again.
    public static void Play(this SpriteAnimator animator, string animation);
}

// What a tile is to the game, as its tileset says.
public enum TileCollision
{
    // Walked through.
    None,
    // Solid on every side: ground, walls.
    Full,
    // Holds up what lands on it from above, lets through what comes from below.
    Top,
}

// The Tilemap components of the scene: grids of cells painted with the tiles of a tileset. Cell
// (x, y) covers [x, x + 1) by [y, y + 1) cells from the origin of its entity, y up.
public static unsafe class Tilemaps
{
    // The flags of a cell that mirror its tile, above its identifier.
    public const int FlipX = 0x4000;
    public const int FlipY = 0x8000;
    // The tile of a cell, its identifier in the tileset; 0 for none.
    public static int GetTile(Entity tilemap, int x, int y);
    // Paints a cell with a tile of the tileset, mirrored or not; 0 empties it.
    public static void SetTile(Entity tilemap, int x, int y, int tile, bool flipX = false, bool flipY = false);
    // The cell under a point of the world.
    public static (int X, int Y) CellAt(Entity tilemap, Vec3 point);
    // The middle of a cell, in the world.
    public static Vec3 CellCenter(Entity tilemap, int x, int y);
    // How the tile of a cell collides, None for an empty cell.
    public static TileCollision GetCollision(Entity tilemap, int x, int y);
    // What the tileset tells the game of the tile of a cell, such as "water"; empty for none.
    public static string GetData(Entity tilemap, int x, int y);
    // The terrain set of the tile of a cell and the terrain of its middle, as its tileset names
    // them; (-1, -1) for an empty cell or a tile outside the terrains.
    public static (int Set, int Terrain) GetTerrain(Entity tilemap, int x, int y);
    // The terrain set and the terrain of that name in the tileset of the tilemap; (-1, -1) when none has it.
    public static (int Set, int Terrain) FindTerrain(Entity tilemap, string name);
    // Paints cells with a terrain of a terrain set, or empties them with -1: each cell takes the tile
    // that matches the cells around it, which change their tiles to match in turn, as the Terrain
    // brush of the editor paints.
    public static void SetTerrain(Entity tilemap, ReadOnlySpan<(int X, int Y)> cells, int set, int terrain);
    public static void SetTerrain(Entity tilemap, int x, int y, int set, int terrain);
    // The same along a path: each cell joins only the one before it and the one after it, in the
    // order given, as a road or a river does.
    public static void SetTerrainPath(Entity tilemap, ReadOnlySpan<(int X, int Y)> cells, int set, int terrain);
}

// The particles of the game: the ParticleEmitter components of the scene, which emit by themselves
// when they play on start, and which code plays, stops and bursts.
public static unsafe class Particles
{
    // Emits again from the start of a cycle, with its burst.
    public static void Play(Entity emitter);
    // Stops emitting; the particles alive finish their life, or vanish with clear.
    public static void Stop(Entity emitter, bool clear = false);
    // Holds the emitter and its particles where they are.
    public static void Pause(Entity emitter);
    public static void Resume(Entity emitter);
    // Emits particles at once, whether the emitter plays or not: sparks where a ball lands.
    public static void Emit(Entity emitter, int count);
    // While it emits, or particles it emitted are alive.
    public static bool IsPlaying(Entity emitter);
    // The particles alive.
    public static int Count(Entity emitter);
    // What pulls every particle down, times the gravity of its emitter: (0, -9.81, 0) at first.
    public static Vec3 Gravity { get; set; }
}

// The interface of the game: the canvases of the scene, what the player clicked this frame, and
// where the focus sits for the keyboard and the pad.
public static unsafe class Ui
{
    // Whether a button carrying this action was clicked during this frame. As many buttons as
    // needed may share an action: any of them answers.
    public static bool WasClicked(string action);
    // Whether that very button was clicked during this frame.
    public static bool WasClicked(Entity entity);
    // Whether a slider carrying this action was moved, or a toggle carrying it turned over,
    // during this frame. The value itself is read from the component of the element.
    public static bool WasChanged(string action);
    // Whether a field carrying this action was ended with Enter during this frame. What was
    // typed is the text of the UiText of the field.
    public static bool WasSubmitted(string action);
    // The field being typed into, or an invalid entity.
    public static Entity EditedField { get; }
    // Whether the player asked to go back this frame: Escape, or the east button.
    public static bool WasCancelled();
    // The button under the pointer, or an invalid entity.
    public static Entity Hovered { get; }
    // The button the keyboard and the pad act on, or an invalid entity.
    public static Entity Focused { get; set; }
    // Whether the pointer rests on the interface, which a game reads before acting on a click of
    // its own.
    public static bool PointerOverInterface { get; }
    // Whether a button carrying this action was clicked twice in a row, quickly, during this
    // frame. The second click also counts as a click.
    public static bool WasDoubleClicked(string action);
    // Whether that very button was clicked twice in a row, quickly, during this frame.
    public static bool WasDoubleClicked(Entity entity);
    // Opens a popup (UiPopup) where its anchors put it.
    public static void OpenPopup(Entity popup);
    // Opens a popup with its top left corner at a point of the screen, in pixels.
    public static void OpenPopup(Entity popup, Vec2 at);
    // Closes a popup. Menus close by themselves; a modal stays until it is closed.
    public static void ClosePopup(Entity popup);
    // Whether a popup is open.
    public static bool IsPopupOpen(Entity popup);
    // The element whose UiContextMenu opened the last context menu, such as the row of a list,
    // which the entries of the menu act on.
    public static Entity ContextTarget { get; }
    // The value of a plot (UiPlot) the pointer is over, from 0: the bar under it, or the point of
    // the line nearest across; -1 when the pointer is elsewhere.
    public static int PlotValueAt(Entity plot);
    // Whether something was dropped during this frame on a drop target (UiDropTarget) carrying
    // this action. What was dropped is in Dropped.
    public static bool WasDropped(string action);
    // Whether something was dropped on that very target during this frame.
    public static bool WasDropped(Entity target);
    // The last drop of this frame, or null when nothing was dropped.
    public static UiDrop? Dropped { get; }
    // The drag source (UiDragSource) the pointer carries, or an invalid entity.
    public static Entity Carried { get; }
    // Colours runs of the text of an area (UiTextArea): the words of a language, the names of a
    // log. Positions count characters of its text, as string.IndexOf does; the letters out
    // of every run keep the colour of the UiText. The runs stay until the next call and do not
    // follow the text when it changes: a script gives them again then, for the lines in view
    // (VisibleTextLines) when the text is long.
    public static void SetTextColors(Entity area, ReadOnlySpan<UiTextSpan> spans);
    // Runs drawn behind the letters of an area, such as what a search found, which stay until the
    // next call.
    public static void SetTextHighlights(Entity area, ReadOnlySpan<UiTextSpan> highlights);
    // Lines an area marks at their left and underlines, such as an error, which stay until the
    // next call.
    public static void SetTextMarks(Entity area, ReadOnlySpan<UiTextLineMark> marks);
    // The first line an area shows, from 0, and how many it shows: the ones worth colouring.
    // Nothing before the area was first laid out.
    public static (int First, int Count) VisibleTextLines(Entity area);
}

// What a drop target took: the drag source it came from, the target, the type and data of the
// source, and where it was let go in the target, from (0, 0) at its top left corner to (1, 1).
public readonly record struct UiDrop(Entity Source, Entity Target, string Type, string Data, Vec2 At);
{
}
```

### 10.2. Coroutines et Tweens

#### 10.2.1. Coroutines (`Coroutines.cs`)

```csharp
public sealed class Coroutine
{
    // It reached its end, failed, or was stopped.
    public bool IsDone { get; private set; }
    // It was stopped, or ended with its component or the scene, before its end.
    public bool IsStopped { get; private set; }
    // Ends it where it waits. Coroutines that await it go on.
    public void Stop();
    // Awaiting a coroutine waits for its end, and throws what it failed with.
    public CoroutineAwaiter GetAwaiter();
}

// What awaiting a coroutine gives.
public readonly struct CoroutineAwaiter : ICriticalNotifyCompletion, IEngineAwaiter
{
    public bool IsCompleted { get; }
    public void GetResult();
    public void OnCompleted(Action continuation);
    public void UnsafeOnCompleted(Action continuation);
}

// The waits of coroutines, which any async method of the game may await too: it then goes on in the
// Update of the frame the wait ends in.
public static class Wait
{
    // Seconds of game time.
    public static WaitAwaiter Seconds(float seconds);
    // The next frame.
    public static WaitAwaiter NextFrame();
    // Until the condition holds, asked once per frame.
    public static WaitAwaiter Until(Func<bool> condition);
    // As long as the condition holds, asked once per frame.
    public static WaitAwaiter While(Func<bool> condition);
}

// What awaiting a wait or a tween gives.
public readonly struct WaitAwaiter : ICriticalNotifyCompletion, IEngineAwaiter
{
    public WaitAwaiter GetAwaiter();
    public bool IsCompleted { get; }
    public void GetResult();
    public void OnCompleted(Action continuation);
    public void UnsafeOnCompleted(Action continuation);
}

public struct CoroutineMethodBuilder
{
    public static CoroutineMethodBuilder Create();
    public Coroutine Task { get; }
    // Runs at once, until the first wait.
    public void Start<TStateMachine>(ref TStateMachine stateMachine) where TStateMachine : IAsyncStateMachine
    public void SetStateMachine(IAsyncStateMachine stateMachine);
    public void SetResult();
    public void SetException(Exception exception);
    public void AwaitOnCompleted<TAwaiter, TStateMachine>(ref TAwaiter awaiter, ref TStateMachine stateMachine);
    public void AwaitUnsafeOnCompleted<TAwaiter, TStateMachine>(ref TAwaiter awaiter, ref TStateMachine stateMachine);
}
```

#### 10.2.2. Tweens (`Tweens.cs`)

```csharp
// How a tween moves between its ends, as the Tweener component's ease names them.
public enum Ease
{
    Linear,
    InQuad,
    OutQuad,
    InOutQuad,
    InCubic,
    OutCubic,
    InOutCubic,
    InSine,
    OutSine,
    InOutSine,
    InExpo,
    OutExpo,
    InOutExpo,
    InBack,
    OutBack,
    InOutBack,
    InElastic,
    OutElastic,
    InOutElastic,
    InBounce,
    OutBounce,
    InOutBounce,
}

// What a tween does once it reaches its end.
public enum TweenLoop
{
    // It ends.
    None,
    // It starts over from its start.
    Restart,
    // It goes back to its start, then forth again.
    PingPong,
}

// A tween: a field of a component of an entity, from one value to another over some time. The field
// holds a number, a vector, a color or a rotation, which is given in degrees around x, y and z.
// <code>
// new TweenSpec(door, "Transform.position", new Vec3(0, 3, 0), 1.5f) { Relative = true, Ease = Ease.InOutSine }.Play();
// </code>
public struct TweenSpec
{
    public Entity Entity;
    // "Component.field": "Transform.position", "UiRect.opacity", "UiImage.color", "PointLight.intensity".
    public string Field;
    // As many components as the field has: X for a number, XYZ for a position, all four for a color.
    public Vec4 To;
    // The start; null starts from the value the field has once the delay is over.
    public Vec4? From;
    // To is added to the start rather than reached.
    public bool Relative;
    // In seconds.
    public float Duration;
    public float Delay;
    public Ease Ease;
    // A curve asset drawn in the editor, which replaces the ease when set.
    public AssetId Curve;
    public TweenLoop Loop;
    // How many times it plays again once over, with a loop; -1 without end.
    public int Repeats;
    public TweenSpec(Entity entity, string field, Vec4 to, float duration);
    public TweenSpec(Entity entity, string field, Vec3 to, float duration);
    public TweenSpec(Entity entity, string field, Vec2 to, float duration);
    public TweenSpec(Entity entity, string field, float to, float duration);
    // Plays it; throws when the entity lacks the component or the field cannot be animated.
    public readonly TweenHandle Play();
}

// A tween or a sequence that plays: what stops it, and what a coroutine awaits.
// <code>await Tween.Fade(panel, 0.0f, 0.3f);</code>
public readonly unsafe struct TweenHandle : IEquatable<TweenHandle>
{
    // While it plays, waits for its delay or is paused.
    public bool IsPlaying { get; }
    // Stops it where it is, or, with complete, at its end.
    public void Kill(bool complete = false);
    public void Pause();
    // Goes on from where Pause held it.
    public void Resume();
    // Awaiting a tween waits for its end, within a coroutine or any async method.
    public WaitAwaiter GetAwaiter();
    public bool Equals(TweenHandle other);
    public override bool Equals(object? other);
    public override int GetHashCode();
    public static bool operator ==(TweenHandle left, TweenHandle right);
    public static bool operator !=(TweenHandle left, TweenHandle right);
}

// Tweens that play one step after the other; the tweens of a step play together.
// <code>
// await new TweenSequence()
// .Append(new TweenSpec(label, "UiRect.opacity", 1.0f, 0.2f))
// .AppendInterval(1.5f)
// .Append(new TweenSpec(label, "UiRect.opacity", 0.0f, 0.4f))
// .Play();
// </code>
public sealed class TweenSequence
{
    // A step of its own, after the others.
    public TweenSequence Append(TweenSpec tween);
    // Plays with the last step, or in a first step.
    public TweenSequence Join(TweenSpec tween);
    // A wait after the last step.
    public TweenSequence AppendInterval(float seconds);
    // Plays it; the handle ends with its last step.
    public unsafe TweenHandle Play();
}

// The tweens of the game: fields of components that go from one value to another over some time,
// written each frame before the interface is laid out. A tween ends with its entity.
public static unsafe class Tween
{
    // Plays a tween; throws when the entity lacks the component or the field cannot be animated.
    public static TweenHandle Play(in TweenSpec tween);
    // Tweens any field that holds a number.
    public static TweenHandle To(Entity entity, string field, float to, float duration, Ease ease = Ease.OutQuad);
    // Tweens any field that holds a vector, a color or a rotation.
    public static TweenHandle To(Entity entity, string field, Vec4 to, float duration, Ease ease = Ease.OutQuad);
    // Moves the entity to a position, relative to its parent.
    public static TweenHandle Move(Entity entity, Vec3 position, float duration, Ease ease = Ease.OutQuad);
    // Moves the entity by an offset.
    public static TweenHandle MoveBy(Entity entity, Vec3 offset, float duration, Ease ease = Ease.OutQuad);
    // Turns the entity to a rotation, in degrees around x, y and z.
    public static TweenHandle Rotate(Entity entity, Vec3 degrees, float duration, Ease ease = Ease.OutQuad);
    public static TweenHandle Scale(Entity entity, Vec3 scale, float duration, Ease ease = Ease.OutQuad);
    // Fades an element of the interface, and those inside it.
    public static TweenHandle Fade(Entity entity, float opacity, float duration, Ease ease = Ease.OutQuad);
    // Starts the Tweener of the entity, which does not start by itself without Play On Start.
    public static TweenHandle PlayTweener(Entity entity);
}
```

### 10.3. Sauvegardes et réglages (`Saves.cs`)

```csharp
// A save as a list of saves shows it.
public sealed class SaveSlot
{
    public required string Name { get; init; }
    // What the game called it when saving: "Chapter 2", the name of a place.
    public required string Label { get; init; }
    // When it was written, in the local time of the machine.
    public required DateTime Time { get; init; }
    // Seconds played until then.
    public required double PlayTime { get; init; }
    public required AssetId Scene { get; init; }
    public required string SceneName { get; init; }
    // The version the game gave what it saved.
    public required int Version { get; init; }
    // Whether loading it brings back the scene it saved.
    public required bool HasScene { get; init; }
    public required bool HasThumbnail { get; init; }
    // The picture of the save as a texture a UiImage shows, or an invalid asset while there is none.
    public AssetId Thumbnail { get; }
}

// The saves of the game, in the folder of the player. A save is an object of the game, whose public
// fields are written as those of a component are, with the scene that plays and a picture of it:
// <code>
// public class Progress { public int Level; public List&lt;string&gt; Items = []; }
// Saves.Save("1", progress, label: "Level 2");
// Progress? loaded = Saves.Load&lt;Progress&gt;("1");
// </code>
// Loading a save that kept its scene puts that scene back at the end of the frame; its Start runs
// with RestoredSlot set, to leave alone what the save brought back. Fields the object no
// longer has are skipped and new ones keep their defaults, so that older saves still load.
public static unsafe class Saves
{
    // Writes the object into the slot, keeping the save it held beside it.
    // <param name="slot">Letters, digits, spaces, dashes, dots and underscores: "1", "auto", "quick".</param>
    // <param name="data">An object whose public fields are saved as those of a component are.</param>
    // <param name="label">Shown in the list of saves: "Chapter 2", the name of a place.</param>
    // <param name="scene">Keeps the scene as it plays, which loading brings back.</param>
    // <param name="thumbnail">Keeps a picture of the next frame, without the interface.</param>
    // <param name="version">The version of what the game saves, to convert older saves once it changes.</param>
    public static void Save<T>(string slot, T data, string label = "", bool scene = true, bool thumbnail = true, int version = 0);
    // Reads the save of the slot, or null when it has none. With restoreScene, the scene it
    // kept replaces the one that plays at the end of the frame, and the time played carries on from it.
    public static T? Load<T>(string slot, bool restoreScene = true) where T : class, new();
    // Every save, the most recent first.
    public static SaveSlot[] List();
    // The save of the slot, or null when it has none.
    public static SaveSlot? Find(string slot);
    public static bool Exists(string slot);
    // Removes the save of the slot, its picture and the save it kept.
    public static void Delete(string slot);
    // The slot the scene that plays came back from, while its Start runs; null otherwise.
    public static string? RestoredSlot { get; }
    // Seconds played, carried on from the save loaded last.
    public static double PlayTime { get; }
    // The picture of the save as a texture a UiImage shows, or an invalid asset while there is none.
    public static AssetId Thumbnail(string slot);
}

// What the player chose, kept from one game to the next in the folder of the player: the volumes, the
// window, and the values the game keeps by name, such as a language or the sensitivity of the mouse.
// The engine applies the volumes at once, full screen outside the editor, and vertical sync from the
// next launch.
public static unsafe class PlayerSettings
{
    // Full screen, or the window; the window as it is while the player has not chosen.
    public static bool Fullscreen { get; set; }
    // Waits for the display between frames; takes effect from the next launch of the game.
    public static bool VSync { get; set; }
    // The volume the player chose for an audio group, or for every sound with Audio.Master: 1 by default.
    public static float GetVolume(string group);
    // Sets the volume the player chose, from 0 to 1; it multiplies the volumes the game sets.
    public static void SetVolume(string group, float volume);
    public static bool GetBool(string key, bool fallback = false);
    public static int GetInt(string key, int fallback = 0);
    public static float GetFloat(string key, float fallback = 0.0f);
    public static string GetString(string key, string fallback = "");
    public static void SetBool(string key, bool value);
    public static void SetInt(string key, int value);
    public static void SetFloat(string key, float value);
    public static void SetString(string key, string value);
    public static bool Has(string key);
    public static void Remove(string key);
}
```

### 10.4. Entrées et actions (`Input.cs`)

```csharp
// A mouse button, as the engine numbers them.
public enum MouseButton
{
    Left = 0,
    Middle = 1,
    Right = 2,
    X1 = 3,
    X2 = 4,
}

// Keyboard and mouse of the current frame. "Pressed" is true only on the frame of the press.
public static unsafe class Input
{
    public static bool IsKeyDown(Key key);
    public static bool WasKeyPressed(Key key);
    public static bool IsMouseButtonDown(MouseButton button);
    public static bool WasMouseButtonPressed(MouseButton button);
    // How far the mouse moved since the previous frame, in pixels.
    public static Vec2 MouseDelta { get; }
    // Where the mouse is in the window, in pixels.
    public static Vec2 MousePosition { get; }
    // A captured mouse is hidden and reports unbounded motion, as a first-person camera needs.
    public static bool MouseCaptured { get; set; }
    // A button held down; an axis or a vector pushed at least halfway.
    public static bool IsActionDown(string action);
    // During the frame the action went down. Read it from Update rather than FixedUpdate.
    public static bool WasActionPressed(string action);
    // During the frame the action went up.
    public static bool WasActionReleased(string action);
    // From -1 to 1 for an axis, 0 or 1 for a button, 0 for a vector.
    public static float ActionAxis(string action);
    // A direction of length 1 at most, with Y up; an axis along X; zero for a button.
    public static Vec2 ActionVector(string action);
    // Turns a context of actions on or off, such as the actions of the game while a menu is open:
    // the actions of a context that is off read as released. Contexts start as the project says,
    // and keep their state from scene to scene.
    public static void SetContextActive(string context, bool active);
    public static bool IsContextActive(string context);
    // How many bindings the action has, in the order of the project settings.
    public static int BindingCount(string action);
    // What to show for a binding: the key as the keyboard prints it, or the name of the button.
    public static string BindingLabel(string action, int binding);
    // Waits for the next key or button the player presses and binds it to the action, in place of
    // one of its bindings: a keyboard binding takes a key or a mouse button, a gamepad one a
    // gamepad button. Escape gives up. The bindings are saved for the next games.
    public static void ListenForBinding(string action, int binding);
    // Whether a binding waits for a key.
    public static bool IsListeningForBinding { get; }
    public static void StopListeningForBinding();
    // Every binding back to those of the project settings.
    public static void ResetBindings();
}
```

### 10.5. Interopération

Le C# n'appelle pas le moteur par `DllImport` : au démarrage du runtime .NET hébergé, le moteur
appelle le point d'entrée de `Devex.Managed` avec un `BootstrapArguments` (`Interop.cs`) qui porte
une **table de pointeurs de fonctions** (`NativeApi`, des `delegate* unmanaged<...>`), remplie côté
C++ dans `engine/src/runtime/ManagedGame.cpp`, et reçoit en retour la table des fonctions managées.
Les deux tables ont le même ordre des deux côtés : une nouvelle fonction s'ajoute **à la fin** de
`NativeApi` dans `ManagedGame.cpp` et dans `Interop.cs`, et `bootstrapVersion` (C++) et
`Bootstrap.Version` (C#) augmentent ensemble (26 aujourd'hui) ; un runtime d'une autre version est
refusé au chargement. Les composants passent par leur mémoire même : le C# lit et écrit les champs
des composants du moteur à leur place dans les pools de la scène, d'après une disposition que le
moteur hache (`ComponentLayoutHash`) pour refuser un assemblage compilé contre une autre.

---

## 11. Conclusion et Résumé Final

### 11.1. Résumé de la Documentation

Ce fichier présente les API **C++** et **C#** du **Devex Engine**, des concepts de base jusqu'aux
modules avancés, avec des tutoriels et des exemples. Les références de la section 10 (et de 4.6)
sont tirées des sources C# ; les choix et leur raison sont dans `docs/decisions.md`, les mesures
dans `docs/performances.md`.

### 11.2. Statistiques de la Documentation

| Catégorie | Nombre | Où |
|----------|--------|-----|
| **Composants du moteur** | 79 | index 8.1 |
| **Services C#** (classes statiques de `Services.cs`) | 20 | section 10.1 |
| **Fonctions natives offertes au C#** | 217 | table `NativeApi`, section 10.5 |
| **Jalons livrés** | 86 | `docs/decisions.md`, *Jalons* |

### 11.3. Architecture Complète du Moteur

```
┌─────────────────────────────────────────────────────────────────────────┐
│                        DEVEX ENGINE - ARCHITECTURE                       │
├─────────────────────────────────────────────────────────────────────────┤
│                                                                          │
│  ┌───────────────────────────────────────────────────────────────────┐  │
│  │                           CORE SYSTEMS                                │  │
│  ├─────────────────┬─────────────────┬─────────────────┬─────────────┤  │
│  │  Scene System   │  ECS System     │  Asset System    │  Runtime   │  │
│  │  (Scene.hpp)    │  (Scene.hpp)    │  (AssetManager)  │  (Game.hpp)│  │
│  └────────┬────────┴────────┬────────┴────────┬────────┴───────┬─────┘  │
│           │                  │                │                │          │  │
│  ┌────────▼────────┐ ┌───────▼───────┐ ┌────────▼──────┐ ┌─────▼──────┐ │
│  │    Entities      │ │   Components  │ │   Assets      │ │  Modules  │ │
│  │    (Entity)      │ │   (View<T>)   │ │   (AssetId)   │ │  (.dll/.so)│ │
│  └─────────────────┘ └───────┬───────┘ └───────────────┘ └───────────┘ │
│                                      │                                   │
│                              ┌───────▼───────┐                           │
│                              │   SYSTEMS     │                           │
│  ┌───────────────────────────────┬───────┬───────┬───────────┐         │
│  │  Physics                     │ Audio │ UI     │ Animation │         │
│  │  PhysicsWorld/Physics2DWorld │       │ UiWorld│AnimWorld │         │
│  └───────────────────────────────┴───────┴───────┴───────────┘         │
│                                                                          │
│  ┌───────────────────────────────────────────────────────────────────┐  │
│  │                         RENDERING SYSTEMS                             │  │
│  ├─────────────────┬─────────────────┬─────────────────┬─────────────┤  │
│  │   Vulkan        │   RenderWorld    │   Materials      │   Shaders   │  │
│  │   (Vulkan.cpp)  │   (RenderWorld)   │   (Material)     │   (.spv)    │  │
│  └─────────────────┴─────────────────┴─────────────────┴─────────────┘  │
│                                                                          │
│  ┌───────────────────────────────────────────────────────────────────┐  │
│  │                        MANAGED CODE (C#)                              │  │
│  ├─────────────────┬─────────────────┬─────────────────┬─────────────┤  │
│  │   GameRuntime   │   Components     │   Services      │   Systems   │  │
│  │   (GameRuntime) │   (Component)    │   (Physics...)  │   (GameSys) │  │
│  └─────────────────┴─────────────────┴─────────────────┴─────────────┘  │
│                                                                          │
│  ┌───────────────────────────────────────────────────────────────────┐  │
│  │                      INTEROPERABILITY LAYER                           │  │
│  ├───────────────────────────────────────────────────────────────────┤  │
│  │  P/Invoke Calls  ◄──────────────────────────►  C++ Native Code     │  │
│  │  (Bootstrap.cs)  │     Memory Mapping       │  (Native Functions) │  │
│  │                  │     Data Marshaling      │                      │  │
│  └───────────────────────────────────────────────────────────────────┘  │
│                                                                          │
└─────────────────────────────────────────────────────────────────────────┘
```

### 11.4. Flow de Développement Recommandé

#### Étape 1: Comprendre l'Architecture ECS
```
1. Lire la section 2 (Système ECS) de cette documentation
2. Étudier les exemples dans la section 6 (Exemples de Code)
3. Créer un petit projet avec 2-3 composants personnalisés
4. Expérimenter avec les Views et les Systèmes
```

#### Étape 2: Développer en C++
```
1. Créer un module de jeu C++ avec CMakeLists.txt
2. Définir vos composants avec DEVEX_REFLECT
3. Enregistrer les composants avec game.component<T>()
4. Créer des systèmes avec game.system()
5. Compiler et tester dans l'éditeur
```

#### Étape 3: Développer en C#
```
1. Créer un projet C# dans le dossier csharp/
2. Héritier de Component pour vos composants
3. Utiliser Entity.Get<T>() pour accéder aux composants
4. Implémenter Start(), Update(), FixedUpdate()
5. Utiliser les attributs [GameSystem] pour les systèmes
```

#### Étape 4: Intégration et Optimisation
```
1. Tester le Hot Reload (F5 dans l'éditeur)
2. Profilez avec le système de profilage intégré
3. Optimiser les boucles chaudes (éviter new, utiliser Views)
4. Gérer correctement la mémoire
5. Valider toutes les couches (ECS, Physique, Rendu, etc.)
```

### 11.5. Bonnes Pratiques de Développement

#### ✅ À Faire
- **Utiliser les Views** : Toujours itérer avec `scene.view<Components...>()`
- **Minimiser les allocations** : Éviter `new` dans les boucles Update
- **Séparer la logique** : Un système = une responsabilité
- **Utiliser les composants** : Une entité = composition de composants
- **Gérer les erreurs** : Utiliser les Result et Error du moteur
- **Documenter le code** : Utiliser les commentaires pour les composants public
- **Tester le Hot Reload** : Essentiel pour le développement rapide

#### ❌ À Éviter
- **Itérer sur toutes les entités** : Utiliser les Views filtrées
- **Stocker des pointeurs bruts** : Utiliser Entity et AssetId
- **Mélanger données et logique** : Séparer composants et systèmes
- **Allouer dans les boucles** : Réutiliser les objets existants
- **Ignorer les warnings** : Résoudre tous les warnings de compilation
- **Créer des dépendances circulaires** : Structurer le code en couches

### 11.6. Patterns de Conception Recommandés

#### Pattern 1: State Machine pour les Entités
```csharp
public class Enemy : Component
{
    public enum State { Idle, Chase, Attack, Dead }
    
    public State CurrentState = State.Idle;
    public Entity Target;
    public float AttackRange = 5.0f;
    public float ChaseSpeed = 3.0f;
    
    public override void Update(float delta)
    {
        switch (CurrentState)
        {
            case State.Idle:
                UpdateIdle(delta);
                break;
            case State.Chase:
                UpdateChase(delta);
                break;
            case State.Attack:
                UpdateAttack(delta);
                break;
            case State.Dead:
                UpdateDead(delta);
                break;
        }
    }
    
    private void UpdateIdle(float delta)
    {
        // Logique d'attente
        if (Target.IsAlive && (Entity.WorldPosition - Target.WorldPosition).Length < 10)
        {
            CurrentState = State.Chase;
        }
    }
    
    // ... autres états
}
```

#### Pattern 2: Singleton Pattern pour les Managers
```cpp
// En C++
class GameManager
{
public:
    static GameManager& instance()
    {
        static GameManager instance;
        return instance;
    }
    
    void up
