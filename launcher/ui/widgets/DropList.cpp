#include "DropList.h"

#include <QDropEvent>
#include <QMimeData>

DropList::DropList(QWidget* parent) : QListWidget(parent)
{
    setAcceptDrops(true);
}

void DropList::dragEnterEvent(QDragEnterEvent* event)
{
    if (event->mimeData()->hasUrls()) event->acceptProposedAction();
}

void DropList::dragMoveEvent(QDragMoveEvent* event)
{
    if (event->mimeData()->hasUrls()) event->acceptProposedAction();
}

void DropList::dragLeaveEvent(QDragLeaveEvent* event)
{
    event->accept();
}

void DropList::dropEvent(QDropEvent* event)
{
    const QMimeData* mimeData = event->mimeData();

    if (!mimeData) {
        return;
    }

    if (mimeData->hasUrls()) {
        auto urls = mimeData->urls();
        emit droppedURLs(urls);
    }

    event->acceptProposedAction();
}
