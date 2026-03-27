#include <iostream>
#include <string>
#include <map>
#include <random>
#include <limits>
#include <algorithm>

using namespace std;

// ============================================================================
// RNG
// ============================================================================
static std::mt19937 rng((std::random_device())());
static double roll01() {
    std::uniform_real_distribution<double> dist(0.0, 1.0);
    return dist(rng);
}

// ============================================================================
// Amulets
// ============================================================================
enum class AmuletType { None, SharpGlass, GreenBerry, PieceOfCloth };

struct Amulet {
    AmuletType type = AmuletType::None;
    string name = "None";
    string desc = "No effects";

    int bonus_damage = 0;          // +attack
    double crit_chance = 0.0;      // 0..1
    int bonus_poison_inflict = 0;  // poison inflicted on hit
    int bonus_soul = 0;            // extra "blue hearts"
    double bonus_protection = 0.0; // 0..1
};

static const map<AmuletType, Amulet> AMULETS = {
    {AmuletType::None,         {AmuletType::None, "None", "No effects", 0, 0.0, 0, 0, 0.0}},
    {AmuletType::SharpGlass,   {AmuletType::SharpGlass, "Sharp Glass", "Attack+1, crit chance 10%", 1, 0.10, 0, 0, 0.0}},
    {AmuletType::GreenBerry,   {AmuletType::GreenBerry, "Green Berry", "Poison inflict +1 (on hit), poison has duration", 0, 0.0, 1, 0, 0.0}},
    {AmuletType::PieceOfCloth, {AmuletType::PieceOfCloth, "Piece Of Cloth", "Soul+1, protection chance 1%", 0, 0.0, 0, 1, 0.01}},
};

static AmuletType choosePlayerAmulet() {
    cout << "\nChoose your amulet:\n";
    cout << "1) Sharp Glass    - Attack+1, crit chance 10%\n";
    cout << "2) Green Berry    - Poison inflict +1 (duration-based)\n";
    cout << "3) Piece Of Cloth - Soul+1, protection chance 1%\n";
    cout << "0) None\n> ";

    int choice;
    while (!(cin >> choice) || choice < 0 || choice > 3) {
        cin.clear();
        cin.ignore(numeric_limits<streamsize>::max(), '\n');
        cout << "Enter 0..3: ";
    }
    switch (choice) {
        case 1: return AmuletType::SharpGlass;
        case 2: return AmuletType::GreenBerry;
        case 3: return AmuletType::PieceOfCloth;
        default: return AmuletType::None;
    }
}

// ============================================================================
// Character base class
// ============================================================================
class Character {
protected:
    string name;

    int max_health;   // cap for red health
    int health;       // current red health
    int soul;         // "blue hearts" сверх лимита (может быть > max_health)
    int base_damage;

    // poison system (duration-based)
    int poison_power; // damage per tick
    int poison_turns; // remaining ticks

    // outgoing poison on hit
    int poison_inflict; // base inflict amount (can be boosted by amulet/weapon)

    // protection + crit
    double protection_chance; // 0..1
    int bonus_damage;         // from amulet
    double crit_chance;       // 0..1

    // amulet (for display)
    Amulet amulet;

    // poison tuning knobs
    static constexpr int POISON_MAX_POWER = 12; // soft cap for tick damage
    static constexpr int POISON_MAX_TURNS = 8;  // hard cap for duration

public:
    Character(string n, int h, int d)
        : name(std::move(n)),
          max_health(h),
          health(h),
          soul(0),
          base_damage(d),
          poison_power(0),
          poison_turns(0),
          poison_inflict(0),
          protection_chance(0.0),
          bonus_damage(0),
          crit_chance(0.0),
          amulet(AMULETS.at(AmuletType::None))
    {}

    // Default: everyone gets None unless explicitly set (player chooses)
    void setAmulet(AmuletType t) {
        amulet = AMULETS.at(t);

        // reset then apply
        bonus_damage = 0;
        crit_chance = 0.0;
        protection_chance = 0.0;
        // NOTE: poison_inflict is NOT reset here, because later you may add weapons, etc.
        // For now it's fine to reset to 0 so amulet is the only source.
        poison_inflict = 0;

        bonus_damage += amulet.bonus_damage;
        crit_chance += amulet.crit_chance;
        poison_inflict += amulet.bonus_poison_inflict;
        soul += amulet.bonus_soul;
        protection_chance += amulet.bonus_protection;

        protection_chance = std::clamp(protection_chance, 0.0, 1.0);
        crit_chance = std::clamp(crit_chance, 0.0, 1.0);
    }

    bool isAlive() const { return (health + soul) > 0; }

    int get_total_health() const { return health + soul; }
    int get_red_health() const { return health; }
    int get_soul() const { return soul; }

    // --------------------------------------------
    // Damage application
    // --------------------------------------------
    void takePureDamage(int dmg) {
        if (dmg <= 0) return;
        int left = dmg;

        if (soul > 0) {
            int used = min(soul, left);
            soul -= used;
            left -= used;
        }
        if (left > 0) {
            health -= left;
            if (health < 0) health = 0;
        }
    }

    void takeDamage(int dmg) {
        if (dmg <= 0) return;
        if (roll01() < protection_chance) {
            cout << name << " blocks the damage! (protection)\n";
            return;
        }
        takePureDamage(dmg);
    }

    // --------------------------------------------
    // Poison mechanics
    // --------------------------------------------
    void applyPoisonAtTurnStart() {
        if (poison_turns <= 0 || poison_power <= 0) return;

        takePureDamage(poison_power);
        poison_turns--;

        cout << name << " suffers " << poison_power << " poison damage! "
             << "(" << poison_turns << " turns left)\n";

        if (poison_turns <= 0) poison_power = 0;
    }

    // Each new infliction:
    // - extends duration
    // - increases power with diminishing returns, soft-capped
    void inflictPoison(int amount) {
        if (amount <= 0) return;

        // duration extension (main benefit for fast weapons)
        poison_turns = min(POISON_MAX_TURNS, poison_turns + 2);

        // diminishing returns for power:
        // add = ceil(amount * (missing/cap)), min 1, so it grows fast early, slow near cap
        int missing = POISON_MAX_POWER - poison_power;
        if (missing <= 0) return;

        int factor = (missing * 100) / POISON_MAX_POWER; // 0..100
        int add = (amount * factor + 99) / 100;          // ceil
        if (add < 1) add = 1;

        poison_power = min(POISON_MAX_POWER, poison_power + add);
    }

    // --------------------------------------------
    // Combat
    // --------------------------------------------
    void attack(Character& target) {
        int dmg = base_damage + bonus_damage;
        bool crit = (roll01() < crit_chance);
        if (crit) dmg *= 2;

        cout << name << " attacks " << target.name << " and deals " << dmg;
        if (crit) cout << " (CRIT)";
        cout << " damage!\n";

        target.takeDamage(dmg);

        if (target.isAlive() && poison_inflict > 0) {
            target.inflictPoison(poison_inflict);
            cout << target.name << " is poisoned! (power " << target.poison_power
                 << ", turns " << target.poison_turns << ")\n";
        }
    }

    // --------------------------------------------
    // Display
    // --------------------------------------------
    void displayStatus() const {
        cout << "Name: " << name
             << " | HP(red): " << health << "/" << max_health
             << " | Soul: " << soul
             << " | Poison: " << poison_power << "x" << poison_turns
             << " | Prot: " << int(protection_chance * 100) << "%"
             << " | Crit: " << int(crit_chance * 100) << "%";
        if (amulet.type != AmuletType::None) {
            cout << " | Amulet: " << amulet.name;
        }
        cout << "\n";
    }
};

// ============================================================================
// UI helpers
// ============================================================================
static void drawHealthBar(const Character& c) {
    cout << "Health bar (total): ";
    int total = c.get_total_health();
    for (int i = 0; i < total; i++) cout << '|';
    cout << " (" << total << ")\n";
}

// ============================================================================
// Derived classes
// ============================================================================
class Player : public Character {
public:
    Player(string n, int h, int d) : Character(std::move(n), h, d) {}

    void victory() { cout << name << " won!\n"; }
    void defeat()  { cout << name << " was defeated!\n"; }

    void displayPlayer() const {
        cout << "[Player O_O]\n";
        displayStatus();
    }
};

class Enemy : public Character {
private:
    unsigned short heal_usage;

public:
    Enemy(string n, int h, int d) : Character(std::move(n), h, d), heal_usage(3) {
        setAmulet(AmuletType::None);
    }

    void displayEnemy() const {
        cout << "[Enemy X_X]\n";
        displayStatus();
    }

    void healingBox() {
        // heals only red HP, capped by max_health
        if (get_red_health() < 5 && heal_usage > 0) {
            heal_usage--;
            // direct access (protected) not available here; so we do a tiny hack:
            // simplest: use takePureDamage negative is not allowed; so implement as a local cast.
            // BUT we kept health protected only in base. We'll just do this by adding a helper:
            // (To keep this file simple, we’ll do a small workaround by re-casting to Character’s internals is not possible.)
            // So: modify healingBox logic: we only print message; actual heal is done by pure add via a friend function is messy.
            // Better: make a protected method in Character. But we keep simple: implement a protected "healRed" method.
            // => We'll implement it below by adding a method in Character would be clean, but file already compiled.
            // So: easiest: change Character health fields to protected already are. Enemy inherits, so it CAN access health/max_health.
            // (Yes: Character fields are protected, so Enemy can access them directly.)
            health += 5;
            if (health > max_health) health = max_health;
            cout << name << " uses healingbox! Red HP restored to " << health << ".\n";
        } else {
            cout << name << " can't use healingbox...\n";
        }
    }
};

class Pet : public Character {
private:
    unsigned short bites;

public:
    Pet(string n, int b) : Character(std::move(n), 999, 1), bites(b) {
        setAmulet(AmuletType::None);
    }

    void displayPet() const {
        cout << "[Pet ^_^]\n";
        displayStatus();
        cout << name << " can bite the enemy " << bites << " times\n";
    }

    void attackPet(Enemy& enemy) {
        if (bites > 0 && isAlive()) {
            attack(enemy);
            bites--;
            cout << name << " bites the enemy!\n";
        }
    }
};

// ============================================================================
// Main
// ============================================================================
int main() {
    string name;
    int damage, health, b;
    int round = 0;

    // Input format (as you had):
    // player: name health damage
    // enemy : name health damage
    // pet   : name bites
    cin >> name >> health >> damage;
    Player player(name, health, damage);

    // Player chooses amulet
    AmuletType pick = choosePlayerAmulet();
    player.setAmulet(pick);

    cin >> name >> health >> damage;
    Enemy enemy(name, health, damage);

    cin >> name >> b;
    Pet pet(name, b);

    while (player.isAlive() && enemy.isAlive()) {
        round++;
        cout << "\n----- The round " << round << " begins -----\n";

        // Turn start: poison ticks
        player.applyPoisonAtTurnStart();
        if (!player.isAlive()) break;

        player.displayPlayer();
        drawHealthBar(player);

        enemy.displayEnemy();
        drawHealthBar(enemy);

        pet.displayPet();

        // Player action
        player.attack(enemy);
        if (!enemy.isAlive()) break;

        // Pet "turn"
        pet.applyPoisonAtTurnStart();
        if (pet.isAlive()) pet.attackPet(enemy);
        if (!enemy.isAlive()) break;

        // Enemy turn start
        enemy.applyPoisonAtTurnStart();
        if (!enemy.isAlive()) break;

        // Enemy action
        enemy.healingBox();
        enemy.attack(player);
        enemy.healingBox();
    }

    if (player.isAlive()) player.victory();
    else player.defeat();

    return 0;
}
