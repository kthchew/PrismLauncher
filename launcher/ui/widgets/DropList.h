#ifndef LAUNCHER_DROPLIST_H
#define LAUNCHER_DROPLIST_H

#include <QListWidget>

class DropList : public QListWidget {
    Q_OBJECT

   public:
    explicit DropList(QWidget* parent = nullptr);

   signals:
    void droppedURLs(QList<QUrl> urls);

   protected:
    void dropEvent(QDropEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dragLeaveEvent(QDragLeaveEvent* event) override;
};

#endif  // LAUNCHER_DROPLIST_H
