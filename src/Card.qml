import QtQuick

// A sheet lifted off the page: three faint offset layers read as a soft
// shadow without a blur pass, so it costs nothing and works on any renderer.
Item {
    id: card
    property QtObject style
    property real radius: 6

    Repeater {
        model: 3
        Rectangle {
            required property int index
            x: -(index + 1) * 1.5
            y: (index + 1) * 2
            width: card.width + (index + 1) * 3
            height: card.height + (index + 1) * 1.5
            radius: card.radius + (index + 1) * 2
            color: card.style.shade
            opacity: 1 - index * 0.3
        }
    }
    Rectangle {
        anchors.fill: parent
        radius: card.radius
        color: card.style.card
        border.width: 1
        border.color: card.style.hairline
    }
}
