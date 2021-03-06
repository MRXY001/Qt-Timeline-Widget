#ifndef TIMELINEWIDGET_H
#define TIMELINEWIDGET_H

#include <QObject>
#include <QDebug>
#include <QInputDialog>
#include <QMenu>
#include <QAction>
#include <QMimeData>
#include <QDrag>
#include <QPropertyAnimation>
#include <QScrollArea>
#include <QScrollBar>
#include <QPlainTextEdit>
#include <QTimer>
#include "timelinebucket.h"
#include "labeleditor.h"

class TimelineWidget : public QScrollArea
{
    Q_OBJECT
public:
    TimelineWidget(QWidget *parent = nullptr);

    void addItem(QString time, QString text);
    void addItem(QString time, QStringList texts);
    TimelineBucket* insertItem(QString time, QStringList texts, int index = -1);
    void removeItem(int index);
    void clearAll();
    int count();
    int indexOf(TimelineBucket* bucket) const;
    TimelineBucket* at(int index) const;
    void moveBucket(int from_index, int to_index);

    void selectAll();
    void unselectAll();
    void selectItem(TimelineBucket* bucket);
    void selectItems(QList<int> rows, bool clearBefore = true);
    void unselectItem(TimelineBucket* bucket);
    void setCurrentItem(int row, bool multi = false);
    void setCurrentItem(TimelineBucket* bucket, bool multi = false);
    void scrollTo(int index = -1);
    QList<int> selectedIndexes(int delta = 0) const;

    void resetWidth();
    void adjustBucketsPositions(int start = -1);
    void adjustBucketsPositionsWithAnimation(int start = 0, int end = -1);

    void fromString(QString string, QString time_reg = "【(.*)】", QString node_split = "\n", QString nodes_split = "\n");
    QString toString(QString time_format = "【%1】", QString para_split = "\n", QString line_split = "\n\n");

protected:
    void keyPressEvent(QKeyEvent* event) override;

private:
    TimelineBucket *createItemWidget(QString time, QStringList texts);

signals:
    void manualSelected(); // 鼠标/键盘事件
    void targetItemsChanged(); // 选中项改变，或者选中的内容改变了

public slots:
    void updateUI();
    void slotBucketWidgetToSelect(TimelineBucket* bucket);
    void slotTimeWidgetClicked(TimelineTimeLabel* label);
    void slotTextWidgetClicked(TimelineTextLabel* label);
    void slotTimeWidgetDoubleClicked(TimelineTimeLabel* label);
    void slotTextWidgetDoubleClicked(TimelineTextLabel* label);
    void slotMenuShowed(const QPoint& pos);
    void slotDroppedAndMoved(TimelineBucket* from, TimelineBucket* to);
    void slotEditChanged();
    void slotEdit(int row, int col);
    void finishEditing();

    void actionAddText();
    void actionAddTextLeft();
    void actionAddTextRight();
    void actionEditTime();
    void actionEditText(int index);
    void actionAddLine();
    void actionInsertAbove();
    void actionInsertUnder();
    void actionDeleteLine();
    void actionCopyText();
    void actionPaste();

private:
    QWidget* center_widget;
    QList<TimelineBucket*> buckets;
    QList<TimelineBucket*> selected_buckets;
    int current_index;

    LabelEditor* edit;
    TimelineBucket* editing_bucket;
    QLabel* editing_label;
    
    bool _adusting_buckets_size; // 是否正在调整索引buckets大小（无视此时触发的sizeHintChanged信号）
    bool _width_need_adjust = false; // 下次动画是否强制调整宽度
};

#endif // TIMELINEWIDGET_H
