TomeNET
=======

TomeNET is an online multiplayer roguelike role-playing game, derived from MAngband (Multiplayer-Angband).

This fork gives Everlasting characters automatic, free resurrection in town
without loss of experience, money, items or royal status. It applies on every
floor, including Morgoth's floor, and to insanity and ghost deaths. Suicide and
retirement still delete the character. Other modes retain their existing rules.

Potions of Skill grant exactly one unspent skill point. Their depth, allocation
chances, weight and price match the six stat-increasing potions. Skill fountains
provide one dose, like stat-increasing fountains.

Run the gameplay regression check with `python tests/check_everlasting.py` from
the repository root, with MinGW GCC, Python and MSYS make on PATH. It uses the
actual server death code, checks both instant-resurrection build settings, and
checks the Skill potion's effect and allocation data. It isolates network/map
I/O. A live server/client test is still needed for deployment.
The death tests also cover this fork's disconnect protection and confirm that
it cannot bypass Everlasting recovery or explicit character deletion.

More information
----------------

Windows package deployment, connection, backup and migration instructions:
[中文部署说明](docs/WINDOWS-DEPLOYMENT.zh-CN.md).

1. Check out the [TomeNET website](https://www.tomenet.eu/).

2. Read the [TomeNET Guide](https://tomenet.eu/guide.php). The guide is also available as a text file [here](TomeNET-Guide.txt).
