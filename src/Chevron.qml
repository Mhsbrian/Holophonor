import QtQuick

// Two hairlines that point down when closed and turn over when open.
Item {
    id: root
    property color color
    property bool open: false
    width: 12
    height: 8
    rotation: open ? 180 : 0
    Behavior on rotation { NumberAnimation { duration: 220; easing.type: Easing.OutCubic } }

    Rectangle {
        x: 0.5
        y: 3
        width: 7
        height: 1.2
        rotation: 35
        antialiasing: true
        color: root.color
    }
    Rectangle {
        x: 4.5
        y: 3
        width: 7
        height: 1.2
        rotation: -35
        antialiasing: true
        color: root.color
    }
}
