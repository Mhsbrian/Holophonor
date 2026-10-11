import QtQuick

// Every shortcut on one card. ? opens it; any key or click puts it away.
Item {
    id: root
    property QtObject style
    property bool shown: false
    signal dismissed

    readonly property var shortcuts: [
        ["/   Ctrl K", "search instruments"],
        ["[   ]", "previous · next instrument"],
        ["V", "choose a visual"],
        ["1 2 3 4", "Sumi · Bloom · Graphite · Projection"],
        ["Space", "a fresh sheet"],
        ["Ctrl S", "save the painting"],
        ["Ctrl D", "play the demo phrase"],
        ["A W S E D … K", "play from the keyboard"],
        ["Z   X", "octave down · up"],
        ["Esc", "silence everything"],
        ["F11", "full screen"],
        ["?", "this card"],
        ["Ctrl Q", "quit"]
    ]

    visible: opacity > 0
    opacity: shown ? 1 : 0
    Behavior on opacity { NumberAnimation { duration: 180 } }

    Rectangle {
        anchors.fill: parent
        color: root.style.dark ? Qt.rgba(0, 0, 0, 0.45) : Qt.rgba(0.95, 0.93, 0.87, 0.6)
    }
    MouseArea {
        anchors.fill: parent
        onClicked: root.dismissed()
    }

    Card {
        anchors.centerIn: parent
        width: grid.width + 72
        height: grid.height + heading.height + 84
        style: root.style

        Text {
            id: heading
            x: 36
            y: 28
            text: "Shortcuts"
            font.family: root.style.serif
            font.italic: true
            font.weight: Font.Light
            font.pixelSize: 28
            color: root.style.ink
        }
        Grid {
            id: grid
            x: 36
            anchors.top: heading.bottom
            anchors.topMargin: 18
            columns: 2
            columnSpacing: 36
            rowSpacing: 9

            Repeater {
                model: root.shortcuts.length * 2
                Text {
                    required property int index
                    readonly property bool isKey: index % 2 === 0
                    text: root.shortcuts[Math.floor(index / 2)][isKey ? 0 : 1]
                    font.family: isKey ? root.style.mono : root.style.serif
                    font.pixelSize: isKey ? 12 : 16
                    font.italic: !isKey
                    color: root.style.ink
                    opacity: isKey ? 0.8 : 0.6
                }
            }
        }
    }
}
