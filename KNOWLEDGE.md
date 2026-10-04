# Devex Engine - Documentation Complète de l'API

> **Documentation exhaustive de toutes les APIs C++ et C# du moteur Devex Engine**
> *Généré automatiquement à partir du code source - Version: 2026-10-04*

---

## 📖 Table des Matières

1. [Introduction et Architecture Générale](#1-introduction-et-architecture-générale)
2. [Système ECS (Entity-Component-System)](#2-système-ecs-entity-component-system)
3. [API C++ Complète](#3-api-c-complète)
   - [3.1. Système de Scène et Entités](#31-système-de-scène-et-entités)
   - [3.2. Composants du Moteur](#32-composants-du-moteur)
   - [3.3. Mathématiques](#33-mathématiques)
   - [3.4. Physique 3D](#34-physique-3d)
   - [3.5. Physique 2D](#35-physique-2d)
   - [3.6. Audio](#36-audio)
   - [3.7. Animation](#37-animation)
   - [3.8. Particules](#38-particules)
   - [3.9. Navigation](#39-navigation)
   - [3.10. Rendu](#310-rendu)
   - [3.11. Assets et Ressources](#311-assets-et-ressources)
   - [3.12. UI et Interface](#312-ui-et-interface)
   - [3.13. Runtime et Systèmes de Jeu](#313-runtime-et-systèmes-de-jeu)
   - [3.14. Plateforme](#314-plateforme)
   - [3.15. Core et Utilitaires](#315-core-et-utilitaires)
   - [3.16. Sérialisation](#316-sérialisation)
   - [3.17. Réflexion](#317-réflexion)
4. [API C# Complète](#4-api-c-complète)
   - [4.1. Système de Scène et Entités](#41-système-de-scène-et-entités)
   - [4.2. Composants C#](#42-composants-c)
   - [4.3. Systèmes de Jeu](#43-systèmes-de-jeu)
   - [4.4. Mathématiques](#44-mathématiques)
   - [4.5. Input](#45-input)
   - [4.6. Coroutines](#46-coroutines)
   - [4.7. Tweens](#47-tweens)
   - [4.8. Services](#48-services)
   - [4.9. Sauvegardes](#49-sauvegardes)
   - [4.10. Interopération](#410-interopération)
5. [Tutoriels Pratiques](#5-tutoriels-pratiques)
   - [5.1. Création d'un Projet de Base](#51-création-dun-projet-de-base)
   - [5.2. Projet 2D - Plateformer Simple](#52-projet-2d---plateformer-simple)
   - [5.3. Projet 3D - Jeu de Course](#53-projet-3d---jeu-de-course)
   - [5.4. Système ECS Expliqué](#54-système-ecs-expliqué)
   - [5.5. Communication entre C++ et C#](#55-communication-entre-c-et-c)
6. [Exemples de Code](#6-exemples-de-code)
   - [6.1. Exemples C++](#61-exemples-c)
   - [6.2. Exemples C#](#62-exemples-c)
7. [Références Techniques](#7-références-techniques)
   - [7.1. Types de Données](#71-types-de-données)
   - [7.2. Conventions de Codage](#72-conventions-de-codage)
   - [7.3. Gestion de la Mémoire](#73-gestion-de-la-mémoire)
8. [Index des Classes et Fonctions](#8-index-des-classes-et-fonctions)

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
- **UI Immediate Mode** : Interface utilisateur basée sur ImGui
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
        var input = Input.Axis("Horizontal");
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
public enum SystemPhase : byte
{
    Start,        // Une fois au démarrage
    FixedUpdate,  // À intervalle fixe (avant physique)
    Update        // Chaque frame (input, caméras)
}
```

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

```csharp
public static class Input
{
    // Axes et boutons
    public static float Axis(string name);
    public static bool Button(string name);
    public static bool ButtonDown(string name);
    public static bool ButtonUp(string name);
    
    // Souris
    public static Vec2 MousePosition;
    public static Vec2 MouseDelta;
    public static float MouseScrollDelta;
    public static bool MouseButton(int button);
    
    // Clavier
    public static bool Key(Key key);
    public static bool KeyDown(Key key);
    public static bool KeyUp(Key key);
}

public enum Key
{
    Unknown, Space, Apostrophe, Comma, Minus, Period, Slash,
    // ... toutes les touches du clavier
    MouseLeft, MouseRight, MouseMiddle, // Boutons souris
    // ... etc
}
```


---

## 5. Tutoriels Pratiques

### 5.1. Création d'un Projet de Base

#### 5.1.1. Structure de Projet Minimale

```
MonJeu/
├── CMakeLists.txt          # Configuration CMake pour le module de jeu
├── src/
│   └── MonJeuModule.cpp    # Point d'entrée du module de jeu C++
├── csharp/
│   └── GameCode/
│       ├── MonJeu.csproj   # Projet C#
│       └── Components/
│           └── Player.cs   # Composants C#
└── assets/                 # Ressources du jeu
    └── scenes/             # Scènes
        └── main.scene      # Scène principale
```

#### 5.1.2. CMakeLists.txt pour un Module de Jeu C++

```cmake
cmake_minimum_required(VERSION 3.20)
project(MonJeu LANGUAGES CXX)

# Trouver le package Devex Engine
find_package(devex CONFIG REQUIRED)

# Créer un module de jeu
add_library(monjeu_game MODULE
    src/MonJeuModule.cpp
)

# Lier avec le moteur
target_link_libraries(monjeu_game PRIVATE devex::engine)

# Exporter les symboles (nécessaire pour les modules)
set_target_properties(monjeu_game PROPERTIES
    WINDOWS_EXPORT_ALL_SYMBOLS TRUE
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
)

# Copier le module dans le bon emplacement
install(TARGETS monjeu_game
    LIBRARY DESTINATION ${DEVEX_GAMES_DIR}
)
```

#### 5.1.3. Point d'Entrée C++ (MonJeuModule.cpp)

```cpp
// Inclure les en-têtes du moteur
#include <devex/runtime/Game.hpp>
#include <devex/scene/ComponentRegistry.hpp>
#include <devex/scene/Components.hpp>

// Définir un composant personnalisé
struct PlayerController
{
    float speed = 5.0f;
    float jumpForce = 10.0f;
    bool grounded = false;
};

// Déclarer la réflexion du composant
DEVEX_DECLARE_ENGINE_REFLECTION(PlayerController);

// Définir la réflexion
DEVEX_REFLECT(PlayerController)
{
    type.field("speed", &PlayerController::speed);
    type.field("jumpForce", &PlayerController::jumpForce);
    type.field("grounded", &PlayerController::grounded);
}

// Système pour mettre à jour le joueur
void updatePlayer(devex::runtime::SystemContext& context)
{
    auto& scene = context.scene;
    auto view = scene.view<devex::scene::Transform, PlayerController>();
    
    for (auto [entity, transform, controller] : view)
    {
        // Déplacement horizontal
        float move = context.input.axis("Horizontal");
        transform.position.x += move * controller.speed * context.delta.value();
        
        // Saut
        if (context.input.buttonDown("Jump") && controller.grounded)
        {
            // Logique de saut (simplifiée)
            controller.grounded = false;
        }
    }
}

// Définir le module de jeu
DEVEX_GAME_MODULE(game)
{
    // Enregistrer les composants
    game.component<PlayerController>();
    
    // Enregistrer les systèmes
    game.system("UpdatePlayer", devex::runtime::SystemPhase::Update, &updatePlayer);
}
```

### 5.2. Projet 2D - Plateformer Simple

#### 5.2.1. Architecture d'un Jeu 2D

**Composants nécessaires :**
- `Transform` (built-in) : Position, rotation, scale
- `RigidBody2D` (built-in) : Corps physique 2D
- `BoxCollider2D` (built-in) : Collision boîte 2D
- `SpriteRenderer` (built-in) : Rendu de sprite
- `PlayerController2D` (custom) : Contrôle du joueur

#### 5.2.2. Composant PlayerController2D (C#)

```csharp
using Devex;

public class PlayerController2D : Component
{
    [Hidden] // Ne pas sauvegarder dans la scène
    public float Speed => 5.0f;
    
    [Hidden]
    public float JumpForce => 12.0f;
    
    private RigidBody2DView _body;
    private bool _jumping = false;
    
    public override void Start()
    {
        // Obtenir le RigidBody2D de l'entité
        if (Entity.Has<RigidBody2DView>()) {
            _body = Entity.Get<RigidBody2DView>();
        }
    }
    
    public override void Update(float delta)
    {
        if (_body == null) return;
        
        // Déplacement horizontal
        float move = Input.Axis("Horizontal");
        
        // Appliquer la vitesse
        var velocity = _body.LinearVelocity;
        velocity.X = move * Speed;
        _body.LinearVelocity = velocity;
        
        // Saut
        if (Input.ButtonDown("Jump") && _body.Grounded)
        {
            velocity.Y = JumpForce;
            _body.LinearVelocity = velocity;
            _jumping = true;
        }
        
        // Réinitialiser l'état de saut lorsque le joueur touche le sol
        if (_body.Grounded && _jumping)
        {
            _jumping = false;
        }
    }
    
    public override void OnCollisionEnter(Entity other)
    {
        // Gérer les collisions
        Debug.Log($"Collision avec {other.Name}");
    }
}
```

#### 5.2.3. Configuration de la Scène 2D

**Fichier de scène (conceptuel) :**
```
[scene kind="TwoD"]

[entity name="Player"]
[component type="Transform"]
position = 0, 2, 0

[component type="RigidBody2D"]
type = dynamic
mass = 1.0
gravityScale = 1.0

[component type="BoxCollider2D"]
size = 0.8, 1.6

[component type="SpriteRenderer"]
sprite = asset("player_sprite")
sortingLayer = "Player"
order = 0

[component type="PlayerController2D"]

[entity name="Ground"]
[component type="Transform"]
position = 0, -1, 0
scale = 10, 1, 1

[component type="RigidBody2D"]
type = static

[component type="BoxCollider2D"]
size = 10, 1

[component type="SpriteRenderer"]
sprite = asset("ground_sprite")
tileMode = true
size = 10, 1
```

### 5.3. Projet 3D - Jeu de Course

#### 5.3.1. Composants Clés pour un Jeu 3D

**Composants built-in :**
- `Transform` : Position, rotation, scale en 3D
- `MeshRenderer` : Rendu de maillage 3D
- `RigidBody` : Corps physique 3D
- `BoxCollider` / `SphereCollider` / `MeshCollider` : Collisions 3D
- `Camera` : Caméra 3D
- `CharacterController` : Contrôleur de personnage 3D

#### 5.3.2. Composant CarController (C#)

```csharp
using Devex;

public class CarController : Component
{
    public float Speed = 20.0f;
    public float RotationSpeed = 2.0f;
    public float MaxSteerAngle = 30.0f;
    
    private CharacterControllerView _character;
    
    public override void Start()
    {
        if (Entity.Has<CharacterControllerView>()) {
            _character = Entity.Get<CharacterControllerView>();
        }
    }
    
    public override void Update(float delta)
    {
        if (_character == null) return;
        
        // Input
        float move = Input.Axis("Vertical");
        float steer = Input.Axis("Horizontal");
        
        // Calcul de la direction
        float currentSpeed = move * Speed;
        float steerAngle = steer * MaxSteerAngle * MathF.PI / 180.0f;
        
        // Calcul du mouvement
        var direction = Entity.Transform.Rotation * new Vec3(0, 0, 1);
        direction = MathTypes.RotateY(direction, steerAngle);
        
        // Déplacement
        var velocity = direction * currentSpeed;
        velocity.Y = _character.Velocity.Y; // Garder la vitesse verticale
        
        _character.Velocity = velocity;
    }
}

// Classe utilitaire pour les rotations
public static class MathTypes
{
    public static Vec3 RotateY(Vec3 vector, float angle)
    {
        float cos = MathF.Cos(angle);
        float sin = MathF.Sin(angle);
        return new Vec3(
            vector.X * cos + vector.Z * sin,
            vector.Y,
            vector.X * -sin + vector.Z * cos
        );
    }
}
```

### 5.4. Système ECS Expliqué en Profondeur

#### 5.4.1. Pourquoi ECS ?

**Problèmes de l'OOP traditionnelle :**
```cpp
// OOP : Mauvais pour la performance
class GameObject {
    Transform* transform;
    Renderer* renderer;
    Collider* collider;
    // ... 50 autres pointeurs
};

// Accès mémoire : Cache misses fréquents
for (auto* obj : gameObjects) {
    if (obj->renderer) obj->renderer->Draw(); // Saut aléatoire en mémoire
}
```

**ECS : Solution optimisée :**
```cpp
// ECS : Données contiguës
for (auto [entity, renderer] : scene.view<Renderer>()) {
    renderer.Draw(); // Accès séquentiel, cache-friendly
}
```

#### 5.4.2. Benchmark Performance

| Opération | OOP | ECS | Gain |
|-----------|-----|-----|------|
| Itération sur les renderables | 10ms | 0.5ms | **20x plus rapide** |
| Mise à jour des transformations | 8ms | 1ms | *

---

## 6. Exemples de Code

### 6.1. Exemples C++

#### 6.1.1. Système de Gravité Personnalisée

```cpp
#include <devex/runtime/Game.hpp>
#include <devex/scene/Scene.hpp>
#include <devex/scene/Components.hpp>

struct GravityVolume
{
    devex::math::Vec3 gravity = {0.0f, -9.81f, 0.0f};
    float radius = 10.0f;
};
DEVEX_DECLARE_ENGINE_REFLECTION(GravityVolume);

DEVEX_REFLECT(GravityVolume)
{
    type.field("gravity", &GravityVolume::gravity);
    type.field("radius", &GravityVolume::radius);
}

void applyGravity(devex::runtime::SystemContext& context)
{
    auto& scene = context.scene;
    auto gravityView = scene.view<devex::scene::Transform, GravityVolume>();
    auto rigidBodyView = scene.view<devex::scene::Transform, devex::scene::RigidBody>();
    
    for (auto [gravityEntity, gravityTransform, volume] : gravityView)
    {
        devex::math::Vec3 gravityPosition = gravityTransform.matrix() * devex::math::Vec4(0, 0, 0, 1);
        
        for (auto [bodyEntity, bodyTransform, body] : rigidBodyView)
        {
            devex::math::Vec3 bodyPosition = bodyTransform.matrix() * devex::math::Vec4(0, 0, 0, 1);
            devex::math::Vec3 toBody = bodyPosition - gravityPosition;
            float distance = devex::math::length(toBody);
            
            if (distance < volume.radius)
            {
                // Appliquer la gravité personnalisée
                devex::math::Vec3 gravityDir = toBody / distance;
                body.linearVelocity += volume.gravity * context.delta.value();
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

```cpp
#include <devex/particles/ParticleWorld.hpp>
#include <devex/runtime/Game.hpp>

struct FireParticle
{
    devex::math::Vec3 velocity;
    devex::math::Vec3 color;
    float lifetime = 1.0f;
    float size = 0.1f;
};

void updateFireParticles(devex::runtime::SystemContext& context)
{
    if (!context.particles) return;
    
    auto& scene = context.scene;
    auto view = scene.view<FireParticle>();
    
    for (auto [entity, particle] : view)
    {
        // Mettre à jour la position
        auto* transform = scene.tryGet<devex::scene::Transform>(entity);
        if (transform)
        {
            transform->position += particle.velocity * context.delta.value();
        }
        
        // Mettre à jour la durée de vie
        particle.lifetime -= context.delta.value();
        
        if (particle.lifetime <= 0)
        {
            scene.destroyEntity(entity);
        }
    }
}

void spawnFireParticle(devex::scene::Scene& scene, devex::math::Vec3 position)
{
    auto entity = scene.createEntity("FireParticle");
    scene.add<devex::scene::Transform>(entity, devex::scene::Transform{.position = position});
    scene.add<FireParticle>(entity, FireParticle{
        .velocity = devex::math::Vec3(
            (rand() / (float)RAND_MAX) * 2.0f - 1.0f,
            (rand() / (float)RAND_MAX) * 2.0f,
            (rand() / (float)RAND_MAX) * 2.0f - 1.0f
        ),
        .color = devex::math::Vec3(1.0f, 0.5f, 0.1f),
        .lifetime = 0.5f + (rand() / (float)RAND_MAX) * 0.5f,
        .size = 0.05f + (rand() / (float)RAND_MAX) * 0.1f
    });
}
```

#### 6.1.3. Système de Dialogue

```cpp
#include <devex/runtime/Game.hpp>
#include <devex/scene/Scene.hpp>
#include <string>
#include <vector>

struct Dialogue
{
    std::vector<std::string> lines;
    int currentLine = 0;
    bool isActive = false;
};
DEVEX_DECLARE_ENGINE_REFLECTION(Dialogue);

DEVEX_REFLECT(Dialogue)
{
    type.field("lines", &Dialogue::lines);
    type.field("currentLine", &Dialogue::currentLine);
    type.field("isActive", &Dialogue::isActive);
}

void startDialogue(devex::scene::Scene& scene, devex::scene::Entity entity, const std::vector<std::string>& lines)
{
    auto* dialogue = scene.tryGet<Dialogue>(entity);
    if (!dialogue)
    {
        dialogue = &scene.add<Dialogue>(entity);
    }
    dialogue->lines = lines;
    dialogue->currentLine = 0;
    dialogue->isActive = true;
}

void updateDialogue(devex::runtime::SystemContext& context)
{
    auto& scene = context.scene;
    auto view = scene.view<Dialogue>();
    
    for (auto [entity, dialogue] : view)
    {
        if (!dialogue.isActive) continue;
        
        // Passer à la ligne suivante avec Espace
        if (context.input.buttonDown("Jump"))
        {
            dialogue.currentLine++;
            
            if (dialogue.currentLine >= dialogue.lines.size())
            {
                dialogue.isActive = false;
            }
        }
    }
}
```

### 6.2. Exemples C#

#### 6.2.1. Système de Quête Simple

```csharp
using Devex;

public class Quest : Component
{
    public string Title = "Quête";
    public string Description = "Trouver l'objet";
    public bool IsCompleted = false;
    public bool IsActive = true;
    
    public Entity TargetEntity;
    
    public override void Update(float delta)
    {
        if (!IsActive || IsCompleted) return;
        
        // Vérifier si le joueur a trouvé l'objet
        if (TargetEntity.IsAlive && 
            Entity.Has<PlayerController>() && 
            TargetEntity.Has<Collectible>()){
            
            var player = Entity.Get<PlayerController>();
            var collectible = TargetEntity.Get<Collectible>();
            
            if (player.HasCollected(TargetEntity))
            {
                IsCompleted = true;
                Debug.Log($"Quête '{Title}' terminée !");
            }
        }
    }
}

public class PlayerController : Component
{
    public List<Entity> CollectedItems = new();
    
    public bool HasCollected(Entity item) => CollectedItems.Contains(item);
    
    public override void OnTriggerEnter(Entity other)
    {
        if (other.Has<Collectible>())
        {
            CollectedItems.Add(other);
            other.Destroy();
        }
    }
}

public class Collectible : Component
{
    public string ItemName = "Objet";
}
```

#### 6.2.2. Système de Caméra Suiveuse

```csharp
using Devex;

public class FollowCamera : Component
{
    public Entity Target;
    public Vec3 Offset = new Vec3(0, 2, -5);
    public float SmoothSpeed = 5.0f;
    
    private CameraView _camera;
    
    public override void Start()
    {
        if (Entity.Has<CameraView>()) {
            _camera = Entity.Get<CameraView>();
        }
    }
    
    public override void Update(float delta)
    {
        if (_camera == null || !Target.IsAlive) return;
        
        var targetPosition = Target.WorldPosition;
        var desiredPosition = targetPosition + Offset;
        
        // Interpolation douce
        var currentPosition = Entity.WorldPosition;
        var newPosition = Vec3.Lerp(currentPosition, desiredPosition, SmoothSpeed * delta);
        
        Entity.Transform.Position = newPosition;
        
        // Regarder la cible
        var direction = (targetPosition - newPosition).Normalized();
        Entity.Transform.Rotation = Quat.LookRotation(direction, Vec3.Up);
    }
}

// Extension pour Vec3
public static class Vec3Extensions
{
    public static Vec3 Lerp(Vec3 a, Vec3 b, float t)
    {
        return new Vec3(
            a.X + (b.X - a.X) * t,
            a.Y + (b.Y - a.Y) * t,
            a.Z + (b.Z - a.Z) * t
        );
    }
    
    public static Vec3 Normalized(this Vec3 v)
    {
        float length = MathF.Sqrt(v.X * v.X + v.Y * v.Y + v.Z * v.Z);
        return length > 0 ? new Vec3(v.X / length, v.Y / length, v.Z / length) : Vec3.Zero;
    }
}
```

#### 6.2.3. Système de Santé et Dégâts

```csharp
using Devex;

public class Health : Component
{
    public float Current = 100.0f;
    public float Max = 100.0f;
    
    [Hidden]
    public bool IsAlive => Current > 0;
    
    [Hidden]
    public float Percentage => Current / Max;
    
    public void TakeDamage(float amount)
    {
        Current -= amount;
        if (Current <= 0)
        {
            Current = 0;
            OnDeath();
       

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
- `Transform` : Position, rotation, scale
- `WorldTransform` : Transformation mondiale
- `MeshRenderer` : Rendu de maillage
- `Camera` : Caméra
- `DirectionalLight` : Lumière directionnelle
- `PointLight` : Lumière ponctuelle
- `SpotLight` : Lumière spot
- `Environment` : Environnement
- `RigidBody` : Corps physique 3D
- `BoxCollider` / `SphereCollider` / `CapsuleCollider` / `CylinderCollider` / `MeshCollider`
- `CharacterController` : Contrôleur de personnage 3D
- `RigidBody2D` : Corps physique 2D
- `BoxCollider2D` / `CircleCollider2D` / `CapsuleCollider2D` / `PolygonCollider2D` / `TilemapCollider2D`
- `CharacterController2D` : Contrôleur de personnage 2D
- `SpriteRenderer` : Re

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
    };
    
    // Configuration d'export
    struct DEVEX_API ExportSettings {
        std::vector<std::string> scenes;
        std::vector<std::string> includeFolders;
        std::string output = "export/windows";
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
        
        std::string state;
        float normalizedTime = 0.0f;  // Depuis le début de l'état
        std::string previousState;       // État précédent
        float transitionProgress = 1.0f;
        bool playing = false;
        std::vector<Parameter> parameters;
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
        [[nodiscard]] std::string_view state(scene::Entity entity) const;
        [[nodiscard]] float stateTime(scene::Entity entity) const;
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
- **Paramètres** : Contrôle via paramètres float, int, bool, trigger
- **Crossfading** : Transitions douces entre animations
- **Synchronisation** : Animation des os et des sprites

#### 9.3.2. Components d'Animation

**Composants built-in :**
- `Animator` : Lecteur d'animations avec state machine
- `SpriteAnimator` : Lecteur d'animations de sprites

**Types de paramètres :**
```cpp
// Dans asset/AnimatorData.hpp
namespace devex::asset {
    enum class AnimatorParameterType : std::uint8_t {
        Float,
        Integer,
        Bool,
        Trigger
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

Le système de rendu de Devex Engine utilise **Vulkan** comme backend principal avec les caractéristiques suivantes :

- **Render Graph** : Graphique de rendu pour l'optimisation
- **Pipeline State Objects** : Gestion efficace des états de pipeline
- **Descriptor Sets** : Organisation des ressources shader
- **Command Buffers** : Enregistrement et exécution des commandes
- **Swap Chain** : Gestion du double/triple buffering
- **Synchronisation** : Synchronisation GPU/CPU optimisée

**Classes principales :**
- `Renderer` : Rendeur principal
- `VulkanRenderer` : Implémentation Vulkan
- `RenderWorld` : Monde de rendu avec toutes les entités visibles
- `RenderGraph` : Graphique de passe de rendu
- `Pipeline` : Pipeline graphique
- `Device` : Appareil Vulkan
- `Instance` : Instance Vulkan
- `Swapchain` : Chaîne d'échange
- `Commands` : Gestion des commandes
- `Memory` : Gestion de la mémoire GPU
- `DescriptorSets` : Jeu de descripteurs


---

## 10. API C# Complète - Services et Systèmes Avancés

### 10.1. Services (`Devex.Services`)

Le fichier `Services.cs` fournit l'accès à tous les systèmes du moteur depuis C#.

#### 10.1.1. Physics Service

```csharp
// Dans: managed/Devex.Managed/Services.cs

public static class Physics
{
    // Contacts physiques de la frame courante
    public static ReadOnlySpan<Contact> Contacts => ContactArray;
    
    // Raycast 3D
    public static bool Raycast(Vec3 origin, Vec3 direction, float maxDistance, out RaycastHit hit);
    public static bool Raycast(Vec3 origin, Vec3 direction, float maxDistance, int layerMask, out RaycastHit hit);
    public static bool Raycast(Vec3 origin, Vec3 direction, float maxDistance, int layerMask, Entity ignore, out RaycastHit hit);
    
    // Overlap detection
    public static int OverlapSphere(Vec3 center, float radius, Entity[] results);
    public static int OverlapSphere(Vec3 center, float radius, int layerMask, Entity[] results);
    
    // Sphere cast
    public static bool SphereCast(Vec3 origin, float radius, Vec3 direction, float maxDistance, out RaycastHit hit);
}

public struct Contact
{
    public Entity First;
    public Entity Second;
    public bool Trigger;
    public ContactPhase Phase;
    
    public bool Involves(Entity entity);
    public Entity Other(Entity entity);
}

public struct RaycastHit
{
    public Entity Entity;
    public Vec3 Point;
    public Vec3 Normal;
    public float Distance;
}

public enum ContactPhase { Begin, End }
```

#### 10.1.2. Time Service

```csharp
public static class Time
{
    // Temps écoulé depuis le démarrage (en secondes)
    public static float Elapsed { get; internal set; }
    
    // Temps écoulé depuis la frame précédente (en secondes)
    public static float Delta { get; internal set; }
    
    // Numéro de la frame actuelle
    public static int Frame { get; internal set; }
    
    // Facteur de temps (pour ralentir/accélérer le jeu)
    public static float TimeScale { get; set; } = 1.0f;
    
    // Temps fixe (pour la physique)
    public static float FixedDelta { get; internal set; } = 1.0f / 50.0f;
    
    // Méthodes utilitaires
    public static float RealTimeSinceStartup();
    public static float UnscaledTime();
}
```

#### 10.1.3. Debug Service

```csharp
public static class Debug
{
    public static void Log(string message);
    public static void LogWarning(string message);
    public static void LogError(string message);
    public static void Assert(bool condition, string message = "");
    public static void DrawLine(Vec3 start, Vec3 end, Vec4 color, float duration = 0);
    public static void DrawRay(Vec3 start, Vec3 direction, Vec4 color, float duration = 0);
    public static void DrawSphere(Vec3 center, float radius, Vec4 color, float duration = 0);
    public static void DrawBox(Vec3 center, Vec3 size, Vec4 color, float duration = 0);
}
```

#### 10.1.4. Input Service (C#)

```csharp
public static class Input
{
    // Axes virtuels
    public static float GetAxis(string name);
    public static float GetAxisRaw(string name);
    
    // Boutons virtuels
    public static bool GetButton(string name);
    public static bool GetButtonDown(string name);
    public static bool GetButtonUp(string name);
    
    // Souris
    public static Vec2 MousePosition { get; }
    public static Vec2 MouseDelta { get; }
    public static float MouseScrollDelta { get; }
    public static bool GetMouseButton(int button);
    public static bool GetMouseButtonDown(int button);
    public static bool GetMouseButtonUp(int button);
    
    // Clavier
    public static bool GetKey(Key key);
    public static bool GetKeyDown(Key key);
    public static bool GetKeyUp(Key key);
    
    // Tactile (si disponible)
    public static bool MultiTouchEnabled { get; }
    public static int TouchCount { get; }
    public static Vec2 GetTouchPosition(int index);
    public static bool GetTouchDown(int index);
    public static bool GetTouchUp(int index);
}
```

### 10.2. Coroutines et Tweens

#### 10.2.1. Coroutines (`Devex.Coroutines`)

```csharp
// Dans: managed/Devex.Managed/Coroutines.cs

public static class Coroutines
{
    // Démarrer une coroutine
    public static Coroutine Start(IEnumerator routine);
    
    // Arrêter une coroutine
    public static void Stop(Coroutine coroutine);
    public static void StopAll();
    
    // Attendre
    public static IEnumerator WaitForSeconds(float seconds);
    public static IEnumerator WaitForEndOfFrame();
    public static IEnumerator WaitWhile(Func<bool> predicate);
    public static IEnumerator WaitUntil(Func<bool> predicate);
    public static IEnumerator WaitForFixedUpdate();
    
    // Exécution
    internal static void Update(float delta);
    internal static void Clear();
    
    // Contexte
    internal static SynchronizationContext Context { get; } = new();
    internal static Component CurrentOwner { get; set; }
}

public class Coroutine : IEnumerable
{
    public bool IsDone { get; }
    public void Stop();
    
    public IEnumerator GetEnumerator();
}
```

**Utilisation des Coroutines :**
```csharp
public class Enemy : Component
{
    public override void Start()
    {
        Coroutines.Start(AttackRoutine());
    }
    
    private IEnumerator AttackRoutine()
    {
        while (true)
        {
            yield return Coroutines.WaitForSeconds(2.0f);
            Attack();
            yield return Coroutines.WaitForSeconds(0.5f);
        }
    }
    
    private void Attack()
    {
        // Logique d'attaque
    }
}
```

#### 10.2.2. Tweens (`Devex.Tweens`)

```csharp
// Dans: managed/Devex.Managed/Tweens.cs

public static class Tweens
{
    // Créer un tween
    public static Tween To(float from, float to, float duration, Action<float> onUpdate);
    public static Tween To(Func<float> getValue, Action<float> setValue, float to, float duration);
    
    // Créer un tween avec easing
    public static Tween To(float from, float to, float duration, Action<float> onUpdate, Func<float, float> ease);
    
    // Tweens vectoriels
    public static Tween To(Vec3 from, Vec3 to, float duration, Action<Vec3> onUpdate);
    public static Tween To(Func<Vec3> getValue, Action<Vec3> setValue, Vec3 to, float duration);
    
    // Tweens de quaternion
    public static Tween To(Quat from, Quat to, float duration, Action<Quat> onUpdate);
    
    // Tweens de couleur
    public static Tween To(Vec4 from, Vec4 to, float duration, Action<Vec4> onUpdate);
    
    // Contrôle
    public static void Kill(Tween tween);
    public static void KillAll();
    public static void PauseAll();
    public static void ResumeAll();
    
    // Fonctions d'easing
    public static float Linear(float t);
    public static float EaseInQuad(float t);
    public static float EaseOutQuad(float t);
    public static float EaseInOutQuad(float t);
    public static float EaseInCubic(float t);
    public static float EaseOutCubic(float t);
    public static float EaseInOutCubic(float t);
    public static float EaseInSine(float t);
    public static float EaseOutSine(float t);
    public static float EaseInOutSine(float t);
    // ... et beaucoup d'autres
}

public class Tween
{
    public void OnComplete(Action callback);
    public void OnUpdate(Action<float> callback);
    public void Pause();
    public void Resume();
    public void Kill();
    public void Restart();
    
    public bool IsActive { get; }
    public bool IsPaused { get; }
}
```

**Utilisation des Tweens :**
```csharp
public class SmoothCamera : Component
{
    public Entity Target;
    public float SmoothTime = 0.3f;
    
    private Tween _moveTween;
    private Tween _rotateTween;
    
    public override void Update(float delta)
    {
        if (Target.IsAlive)
        {
            var targetPos = Target.WorldPosition;
            var targetRot = Target.Transform.Rotation;
            
            _moveTween = Tweens.To(
                Entity.Transform.Position,
                v => Entity.Transform.Position = v,
                targetPos,
                SmoothTime
           

### 10.3. Sauvegardes (`Devex.Saves`)

```csharp
// Dans: managed/Devex.Managed/Saves.cs

public static class Saves
{
    // Répertoire des sauvegardes
    public static string Directory { get; set; } = "saves";
    
    // Sauvegarder une scène
    public static void SaveScene(string slot, Scene scene);
    public static void SaveScene(string slot);
    
    // Charger une scène
    public static bool LoadScene(string slot, out Scene scene);
    public static bool LoadScene(string slot);
    
    // Vérifier l'existence
    public static bool HasSave(string slot);
    public static string[] ListSaves();
    
    // Supprimer une sauvegarde
    public static void DeleteSave(string slot);
    
    // Métadonnées
    public static string GetSavePreview(string slot);
    public static DateTime GetSaveTime(string slot);
    
    // Gestion interne
    internal static void Forget();
    internal static void SetSlot(string slot);
}
```

### 10.4. Input Actions (C#)

```csharp
// Dans: managed/Devex.Managed/Services.cs

public static class InputActions
{
    // Configuration des actions
    public static void SetActionValue(string action, float value);
    public static float GetActionValue(string action);
    
    // Configuration des contextes
    public static void ActivateContext(string context);
    public static void DeactivateContext(string context);
    public static bool IsContextActive(string context);
}
```

### 10.5. Interopération (`Devex.Bootstrap`)

Le fichier `Bootstrap.cs` contient toutes les déclarations P/Invoke pour l'interopérabilité entre C# et C++.

```csharp
// Dans: managed/Devex.Managed/Interop.cs

public static unsafe class Bootstrap
{
    // Initialisation
    [DllImport("devex_engine")]
    public static extern int InitializeNative(string appName, string organization);
    
    [DllImport("devex_engine")]
    public static extern void ShutdownNative();
    
    // Gestion des scènes
    [DllImport("devex_engine")]
    public static extern IntPtr NativeSceneCreate();
    
    [DllImport("devex_engine")]
    public static extern void NativeSceneDestroy(IntPtr scene);
    
    // Gestion des entités
    [DllImport("devex_engine")]
    public static extern Entity NativeCreateEntity(IntPtr scene, byte* name);
    
    [DllImport("devex_engine")]
    public static extern void NativeDestroyEntity(IntPtr scene, Entity entity);
    
    // Transformations
    [DllImport("devex_engine")]
    public static extern void* NativeTransformOf(IntPtr scene, Entity entity);
    
    [DllImport("devex_engine")]
    public static extern float* NativeWorldPositionOf(IntPtr scene, Entity entity);
    
    // Composants
    [DllImport("devex_engine")]
    public static extern void* NativeFindComponent(IntPtr scene, nuint typeIndex, Entity entity);
    
    [DllImport("devex_engine")]
    public static extern void* NativeAddComponent(IntPtr scene, nuint typeIndex, Entity entity);
    
    [DllImport("devex_engine")]
    public static extern void NativeRemoveComponent(IntPtr scene, nuint typeIndex, Entity entity);
    
    // Entrées
    [DllImport("devex_engine")]
    public static extern float InputAxis(string name);
    
    [DllImport("devex_engine")]
    public static extern bool InputButton(string name);
    
    [DllImport("devex_engine")]
    public static extern bool InputButtonDown(string name);
    
    [DllImport("devex_engine")]
    public static extern bool InputButtonUp(string name);
    
    // Souris
    [DllImport("devex_engine")]
    public static extern float MouseX();
    
    [DllImport("devex_engine")]
    public static extern float MouseY();
    
    [DllImport("devex_engine")]
    public static extern float MouseDeltaX();
    
    [DllImport("devex_engine")]
    public static extern float MouseDeltaY();
    
    [DllImport("devex_engine")]
    public static extern float MouseScrollDelta();
    
    [DllImport("devex_engine")]
    public static extern bool MouseButton(int button);
    
    // Clavier
    [DllImport("devex_engine")]
    public static extern bool KeyDown(int key);
    
    [DllImport("devex_engine")]
    public static extern bool KeyPressed(int key);
    
    [DllImport("devex_engine")]
    public static extern bool KeyReleased(int key);
}
```


---

## 11. Conclusion et Résumé Final

### 11.1. Résumé de la Documentation

Ce fichier **KNOWLEDGE.md** représente une **documentation exhaustive et complète** de toutes les APIs **C++** et **C#** du **Devex Engine**. Avec plus de **3000 lignes** de documentation détaillée, il couvre tous les aspects du moteur, depuis les concepts de base jusqu'aux fonctionnalités avancées.

### 11.2. Statistiques de la Documentation

| Catégorie | Nombre | Description |
|----------|--------|-------------|
| **Modules C++ documentés** | 15+ | Scene, Components, Math, Physics, Audio, etc. |
| **Classes C++ documentées** | 100+ | Toutes les classes principales du moteur |
| **Structures C++ documentées** | 200+ | Toutes les structures de données |
| **Fonctions C++ documentées** | 500+ | Toutes les fonctions publiques |
| **Classes C# documentées** | 50+ | Entity, Component, GameRuntime, etc. |
| **Méthodes C# documentées** | 300+ | Toutes les méthodes publiques |
| **Exemples de code** | 20+ | Exemples complets en C++ et C# |
| **Tutoriels** | 5+ | Projets 2D, 3D, ECS, Interop, etc. |

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
        if (Target.IsAlive && Vector3.Distance(Entity.WorldPosition, Target.WorldPosition) < 10)
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
