#include "timelinewidget.h"

TimelineWidget::TimelineWidget(QWidget *parent) : QScrollArea(parent)
{
    setAcceptDrops(true);
    setAttribute(Qt::WA_TransparentForMouseEvents, false);
    setContextMenuPolicy(Qt::CustomContextMenu);
    setFocusPolicy(Qt::StrongFocus);
    connect(this,SIGNAL(customContextMenuRequested (const QPoint&)),this,SLOT(slotMenuShowed(const QPoint&)));

    _adusting_buckets_size = false;
    _width_need_adjust = false;
    current_index = -1;
    center_widget = new QWidget(this);
    setWidget(center_widget);

    editing_bucket = nullptr;
    editing_label = nullptr;
    edit = new LabelEditor(center_widget);
    connect(edit, &LabelEditor::textChanged, this, [=] {
        if (editing_label == nullptr)
            return ;
        editing_label->setText(edit->toPlainText());
//        if (editing_label->objectName() == "TimelineTextLabel") {
//            static_cast<TimelineTextLabel>(editing_label).adjustSize(true, edit->toPlainText());
//        } else {
            editing_label->adjustSize();
//        }
        edit->move(editing_label->pos() + editing_label->parentWidget()->pos());
        edit->resize(editing_label->size());
        editing_bucket->adjustWidgetsPositions();
    });
    connect(edit, &LabelEditor::signalEditCanceled, this, [=](QString origin) {
        if (editing_label == nullptr)
            return ;
        edit->setPlainText(origin); // 设置回初始内容
        QTimer::singleShot(0, [=]{
            editing_label = nullptr;
            editing_bucket = nullptr;
        });
        this->setFocus();
    });
    connect(edit, &LabelEditor::signalEditFinished, this, [=](QString text) {
        if (editing_label == nullptr) // 快速按下两次时会触发这个信号槽，而第一次已经使 editing_label = nullptr
            return ;
        // 编辑结束，保存 undo
        QString orig = edit->getOriginText();
        if (text != orig) // 文本有变动
        {
            if (editing_bucket->indexOf(static_cast<TimelineTextLabel*>(editing_label)) >= 0)
            {
                timeline_undos->modifyCommand(editing_bucket, static_cast<TimelineTextLabel*>(editing_label), orig, text);
                emit targetItemsChanged();
            }
            else
            {
                timeline_undos->modifyCommand(editing_bucket, orig, text);
                emit targetItemsChanged();
            }
        }
        edit->hide();
        editing_label = nullptr;
        editing_bucket = nullptr;
        this->setFocus();
    });
    edit->hide();

    updateUI();
}

void TimelineWidget::addItem(QString time, QString text)
{
    addItem(time, QStringList{text});
}

void TimelineWidget::addItem(QString time, QStringList texts)
{
    insertItem(time, texts, -1);
}

TimelineBucket *TimelineWidget::insertItem(QString time, QStringList texts, int index)
{
    TimelineBucket* bucket = createItemWidget(time, texts);
    bucket->adjustWidgetsPositions();
    if (index < 0 || index >= count()) // 添加到末尾
    {
        if (count() >= 1)
            bucket->move(buckets.last()->pos());
        buckets.append(bucket);
        bucket->setVerticalIndex(count()-1); // 已经添加了，下标索引要-1
        if (count())
            bucket->move(buckets.last()->geometry().topLeft());
    }
    else // 插入到中间
    {
        buckets.insert(index, bucket);
        for (int i = index; i < count(); i++)
            buckets.at(i)->setVerticalIndex(i);
        if (index+1 < count())
            bucket->move(buckets.at(index+1)->geometry().topLeft());
        else if (index > 0)
            bucket->move(buckets.at(index-1)->geometry().topLeft());
    }

    bucket->show();

    // 设置item的尺寸
    connect(bucket, &TimelineBucket::signalSizeHintChanged, this, [=](QSize size){
        if (!_adusting_buckets_size)
            adjustBucketsPositions(buckets.indexOf(bucket));
    });

    // 连接事件信号
    connect(bucket, &TimelineBucket::signalBucketWidgetPressed, this, [=]{ slotBucketWidgetToSelect(bucket); });
    connect(bucket, SIGNAL(signalTimeWidgetClicked(TimelineTimeLabel*)), this, SLOT(slotTimeWidgetClicked(TimelineTimeLabel*)));
    connect(bucket, SIGNAL(signalTextWidgetClicked(TimelineTextLabel*)), this, SLOT(slotTextWidgetClicked(TimelineTextLabel*)));
    connect(bucket, SIGNAL(signalTimeWidgetDoubleClicked(TimelineTimeLabel*)), this, SLOT(slotTimeWidgetDoubleClicked(TimelineTimeLabel*)));
    connect(bucket, SIGNAL(signalTextWidgetDoubleClicked(TimelineTextLabel*)), this, SLOT(slotTextWidgetDoubleClicked(TimelineTextLabel*)));

    connect(bucket, &TimelineBucket::signalDroppedAndMoved, this, [=](TimelineBucket* from_bucket) {
        slotDroppedAndMoved(from_bucket, bucket);
    });

    return bucket;
}

void TimelineWidget::removeItem(int index)
{
    if (index < 0 || index >= count())
        return ;

    auto bucket = buckets.takeAt(index);
    selected_buckets.removeOne(bucket);
    bucket->deleteLater();

    adjustBucketsPositionsWithAnimation(index);
}

void TimelineWidget::clearAll()
{
    while (buckets.size())
    {
        buckets.takeFirst()->deleteLater();
    }
}

int TimelineWidget::count()
{
    return buckets.size();
}

int TimelineWidget::indexOf(TimelineBucket *bucket) const
{
    return buckets.indexOf(bucket);
}

TimelineBucket *TimelineWidget::at(int index) const
{
    if (index < 0 || index >= buckets.size())
        return nullptr;
    return buckets.at(index);
}

void TimelineWidget::moveBucket(int from_index, int to_index)
{
    if (from_index == to_index) // 很可能发生的自己和自己交换
        return ;
    if (from_index < 0 || to_index < 0)
        return ;
    finishEditing();

    // 交换 bucket
    TimelineBucket* bucket = buckets.at(from_index);
    buckets.removeAt(from_index);
    if (from_index < to_index) // 下移
    {
        buckets.insert(to_index, bucket);
        for (int i = from_index; i <= to_index; i++)
            buckets.at(i)->setVerticalIndex(i);
    }
    else // 上移
    {
        buckets.insert(to_index, bucket);
        for (int i = from_index; i >= to_index; i--)
            buckets.at(i)->setVerticalIndex(i);
    }

    adjustBucketsPositionsWithAnimation(qMin(from_index, to_index));
}

void TimelineWidget::selectAll()
{
    int left = horizontalScrollBar()->sliderPosition(),
            right = horizontalScrollBar()->sliderPosition() + horizontalScrollBar()->pageStep();
    bool odd = true;
    foreach (TimelineBucket* bucket, buckets) {
        if (odd)
            bucket->setPressPos(QPoint(left, bucket->height()/2));
        else
            bucket->setPressPos(QPoint(right, bucket->height()/2));
        odd = !odd;
        bucket->setSelected(true);
    }
    selected_buckets = buckets;
    emit targetItemsChanged();
}

void TimelineWidget::unselectAll()
{
    foreach (TimelineBucket* bucket, selected_buckets) {
        bucket->setSelected(false);
    }
    selected_buckets.clear();
    emit targetItemsChanged();
}

void TimelineWidget::selectItem(TimelineBucket *bucket)
{
    bucket->setSelected(true);
    if (!selected_buckets.contains(bucket))
        selected_buckets.append(bucket);
    emit targetItemsChanged();
}

void TimelineWidget::selectItems(QList<int> rows, bool clearBefore)
{
    if (clearBefore)
        unselectAll();
    foreach (auto row, rows)
    {
        auto bucket = buckets.at(row);
        bucket->setSelected(true);
        if (!selected_buckets.contains(bucket))
            selected_buckets.append(bucket);
    }
    emit targetItemsChanged();
}

void TimelineWidget::unselectItem(TimelineBucket *bucket)
{
    bucket->setSelected(false);
    selected_buckets.removeOne(bucket);
    emit targetItemsChanged();
}

void TimelineWidget::setCurrentItem(int row, bool multi)
{
    if (!multi)
        unselectAll();
    selectItem(buckets.at(row));
    current_index = row;
}

void TimelineWidget::setCurrentItem(TimelineBucket *bucket, bool multi)
{
    if (!multi)
        unselectAll();
    selectItem(bucket);
    current_index = buckets.indexOf(bucket);
}

/**
 * 确保某个bucket可视
 */
void TimelineWidget::scrollTo(int index)
{
    if (index == -1)
        index = current_index;
    if (index == -1)
        return ;
    auto bucket = buckets.at(index);
    int h = bucket->height();
    int top = bucket->pos().y();
    int bottom = bucket->geometry().bottom();
    if (top - h < verticalScrollBar()->sliderPosition()) // 在上面
    {
        verticalScrollBar()->setSliderPosition(top - h);
    }
    else if (bottom + h > verticalScrollBar()->sliderPosition() + verticalScrollBar()->pageStep()) // 在下面
    {
        verticalScrollBar()->setSliderPosition(bottom + h - verticalScrollBar()->pageStep());
    }
}

QList<int> TimelineWidget::selectedIndexes(int delta) const
{
    int size = buckets.size();
    QList<int> indexes;
    for (int i = 0; i < size; i++)
        if (buckets.at(i)->isSelected())
            indexes << (i+delta);
    return indexes;
}

void TimelineWidget::resetWidth()
{
    _width_need_adjust = true;
}

/**
 * 调整某一个位置及后面的所有top
 */
void TimelineWidget::adjustBucketsPositions(int start)
{
    int end = count();
    int top = (start-1) >= 0 ? buckets.at(start-1)->geometry().bottom() : 0;
    int max_width = 0;
    int current_width = center_widget->width();
    if (start > 0)
        max_width = center_widget->width();
    for (int i = start; i < end; i++)
    {
        TimelineBucket* bucket = buckets.at(i);
        if (max_width < bucket->width())
            max_width = bucket->width();
        bucket->move(bucket->pos().x(), top);
        top += bucket->height();
    }

    _adusting_buckets_size = true;
    {
        if (max_width != current_width || _width_need_adjust)
        {
            foreach (auto bucket, buckets)
            {
                bucket->resize(max_width, bucket->height());
            }
        }

        int height = 0;
        if (buckets.size())
            height = top + buckets.last()->height();
        else
            height = 50;
        center_widget->resize(max_width, height);
    }
   _adusting_buckets_size = false;
}

/**
 * 调整某一范围内 buckets 的位置
 * 并且包含位置移动动画
 */
void TimelineWidget::adjustBucketsPositionsWithAnimation(int start, int end)
{
    if (end == -1)
        end = count();
    else
        end++;
    int top = (start-1) >= 0 ? buckets.at(start-1)->geometry().bottom() : 0;
    int current_width = center_widget->width();
    int max_width = 0;
    if (start > 0)
        max_width = center_widget->width();
    for (int i = start; i < end; i++)
    {
        TimelineBucket* bucket = buckets.at(i);
        if (max_width < bucket->width())
            max_width = bucket->width();
        if (top != bucket->pos().y())
        {
            QPropertyAnimation* ani = new QPropertyAnimation(bucket, "pos");
            ani->setStartValue(bucket->pos());
            ani->setEndValue(QPoint(bucket->pos().x(), top));
            ani->setDuration(300);
            ani->setEasingCurve(QEasingCurve::OutQuart);
            connect(ani, SIGNAL(finished()), ani, SLOT(deleteLater()));
            ani->start();
        }
        top += bucket->height();
    }

    // 这句会在启动时触发 signalSizeHintChanged，但是必须需要啊
    // _adusting_buckets_size = true;
    {
        if (max_width != current_width || _width_need_adjust)
        {
            foreach (auto bucket, buckets)
            {
                bucket->resize(max_width, bucket->height());
            }
        }

        int height = 0;
        if (buckets.size())
            height = top + buckets.last()->height();
        else
            height = 50;
        center_widget->resize(max_width, height);
    }
    // _adusting_buckets_size = false;
}

/**
 * 从字符串中读取
 * @param string       带格式的字符串
 * @param time_format  获取时间正则表达式，以第一个括号确定（不要带有 ^ $ 标记！）
 * @param node_split   时间节点内分段格式
 * @param nodes_split  时间节点之间分段格式
 */
void TimelineWidget::fromString(QString string, QString time_reg, QString node_split, QString nodes_split)
{
    QList<QString> times;
    QList<QStringList> textss;

    if (node_split == nodes_split) // 分段符一致，以每一段的时间标记为准
    {
        QString time, time_total;
        QStringList texts;

        QRegExp rx(time_reg);
        rx.setMinimal(true);
        
        QStringList lines = string.split(nodes_split, QString::SkipEmptyParts);
        for (int i = 0; i < lines.length(); i++)
        {
            QString& line = lines[i];
            int pos = rx.indexIn(line);
            if (pos != -1) // 找到时间标记
            {
                // 添加上一个时间轴
                if (time != nullptr || texts.length() > 0)
                {
                    times.append(time);
                    textss.append(texts);
                    time = "";
                    texts.clear();
                }
                
                time_total = rx.cap(0);
                time = rx.cap(1);

                // 删除行内标记
                QRegExp ex(time_total + "[\\s　]*");
                line.replace(ex, "");
                if (!line.trimmed().isEmpty()) // 这一段还有其他内容，继续便利
                    i--;
            }
            else
            {
                texts.append(line.trimmed());
            }
        }
        if (time != nullptr || texts.length() > 0)
        {
            times.append(time);
            textss.append(texts);
            time = "";
            texts.clear();
        }
    }
    else // 根据分割来
    {
        QStringList lines = string.split(nodes_split, QString::SkipEmptyParts);
        foreach (QString line, lines)
        {
            QString time_total, time; // 带格式的时间字符串、纯时间字符串
            QStringList texts;
            QRegExp rx(time_reg);
            rx.setMinimal(true);
            int pos = rx.indexIn(line);
            if (pos != -1)
            {
                time_total = rx.cap(0);
                time = rx.cap(1);

                // 删除时间标记
                QRegExp ex(time_total + "[\\s　]*");
                line.replace(ex, "");
            }
            texts = line.split(node_split, QString::SkipEmptyParts);
            for (int i = 0; i < texts.size(); i++)
            {
                texts[i] = texts[i].trimmed();
            }

            times.append(time);
            textss.append(texts);
        }
    }

    QList<int> indexs;
    int c = count();
    for (int i = 0; i < times.size(); i++)
        indexs.append(c);
    timeline_undos->addCommand(indexs, times, textss);
}

/**
 * 将时间轴转换成带分段格式的字符串
 * @param time_format 时间格式，以 %1 确定
 * @param para_split  同一时间节点内分段格式
 * @param line_split  时间节点之间的分段格式
 * @return 所有字符串
 */
QString TimelineWidget::toString(QString time_format, QString para_split, QString line_split)
{
    QString result;
    foreach (auto bucket, buckets)
    {
        if (!result.isEmpty())
            result += line_split;
        result += bucket->toString(time_format, para_split);
    }
    return result;
}

void TimelineWidget::keyPressEvent(QKeyEvent *event)
{
    auto modifiers = event->modifiers();
    auto key = event->key();
    switch (key)
    {
    case Qt::Key_Up:
        if (current_index > 0)
        {
            if (modifiers == Qt::NoModifier) // 上移选中项
            {
                auto bucket = buckets.at(current_index-1);
                bucket->setPressPos(QPoint(qMin(bucket->width(), horizontalScrollBar()->pageStep()), bucket->height()));
                setCurrentItem(current_index-1);
                scrollTo();
                emit manualSelected();
                return ;
            }
            else if (modifiers == Qt::ShiftModifier) // 上移并选中/取消你
            {
                auto bucket = buckets.at(current_index);
                auto bucket_up = buckets.at(current_index-1);
                if (bucket->isSelected() && bucket_up->isSelected())
                {
                    unselectItem(bucket);
                    current_index--;
                }
                else
                {
                    bucket_up->setPressPos(QPoint(qMin(bucket_up->width(), horizontalScrollBar()->pageStep()), bucket_up->height()));
                    setCurrentItem(current_index-1, true);
                }
                scrollTo();
                emit manualSelected();
                return ;
            }
        }
        break;
    case Qt::Key_Down:
        if (current_index > -1 && current_index < count()-1)
        {
            if (modifiers == Qt::NoModifier) // 下移选中项
            {
                auto bucket = buckets.at(current_index+1);
                bucket->setPressPos(QPoint(qMin(bucket->width(), horizontalScrollBar()->pageStep()), 0));
                setCurrentItem(current_index+1);
                scrollTo();
                emit manualSelected();
                return ;
            }
            else if (modifiers == Qt::ShiftModifier) // 下移并选中/取消
            {
                auto bucket = buckets.at(current_index);
                auto bucket_down = buckets.at(current_index+1);
                if (bucket->isSelected() && bucket_down->isSelected())
                {
                    unselectItem(bucket);
                    current_index++;
                }
                else
                {
                    bucket_down->setPressPos(QPoint(qMin(bucket_down->width(), horizontalScrollBar()->pageStep()), 0));
                    setCurrentItem(current_index+1, true);
                }
                scrollTo();
                emit manualSelected();
                return ;
            }
        }
        break;
    case Qt::Key_Home:
        if (current_index > 0 && modifiers == Qt::ShiftModifier)
        {
            int index = current_index;
            while (index >= 0)
            {
                setCurrentItem(index, true);
                index--;
            }
            scrollTo();
            emit manualSelected();
            return ;
        }
        break;
    case Qt::Key_End:
        if (current_index > -1 && modifiers == Qt::ShiftModifier)
        {
            int index = current_index;
            while (index < count())
            {
                setCurrentItem(index, true);
                index++;
            }
            scrollTo();
            emit manualSelected();
            return ;
        }
        break;
    case Qt::Key_Delete:
    {
        int index = current_index;
        actionDeleteLine();
        // 删除键删除的需要继续保持选中状态
        if (index > -1 && index < count()) // 聚焦原来的同一个索引
            setCurrentItem(index);
        else if (index > 0 && index == count()) // 聚焦最后一个
            setCurrentItem(index-1);
        return ;
    }
    case Qt::Key_Insert:
        actionInsertAbove();
        return ;
    case Qt::Key_Escape:
        if (current_index > -1)
        {
            if (selected_buckets.size() > 1)
                setCurrentItem(current_index);
            else if (selected_buckets.size())
                unselectItem(buckets.at(current_index));
            return ;
        }
        else
        {
            unselectAll();
        }
        break;
    case Qt::Key_A:
        if (modifiers == Qt::ControlModifier)
        {
            selectAll();
            return ;
        }
        break;
    case Qt::Key_Z:
        if (modifiers == Qt::ControlModifier)
        {
            timeline_undos->undoCommand();
            return ;
        }
        break;
    case Qt::Key_Y:
        if (modifiers == Qt::ControlModifier)
        {
            timeline_undos->redoCommand();
            return ;
        }
        break;
    case Qt::Key_C:
        actionCopyText();
        return ;
    case Qt::Key_Tab:
        /**
         * 注意：如果要监听到 Tab 键，要禁止 Tab 切换
         * QWidget::setFocus(Qt::NoFocus)
         */
        if (modifiers == Qt::NoModifier)
        {
            actionAddText();
        }
        return ;
    case Qt::Key_Enter:
    case Qt::Key_Return:
        if (modifiers == Qt::ShiftModifier)
            actionInsertAbove();
        else if (modifiers == Qt::ControlModifier)
            actionAddLine();
        else if (modifiers == Qt::NoModifier)
            actionInsertUnder();
        return ;
    case Qt::Key_Space:
        actionEditText(0);
        return ;
    case Qt::Key_1:
        actionEditText(0);
        return ;
    case Qt::Key_2:
        actionEditText(1);
        return ;
    case Qt::Key_3:
        actionEditText(2);
        return ;
    case Qt::Key_4:
        actionEditText(3);
        return ;
    case Qt::Key_5:
        actionEditText(4);
        return ;
    case Qt::Key_6:
        actionEditText(5);
        return ;
    case Qt::Key_7:
        actionEditText(6);
        return ;
    case Qt::Key_8:
        actionEditText(7);
        return ;
    case Qt::Key_9:
        actionEditText(8);
        return ;
    case Qt::Key_0:
    case Qt::Key_QuoteLeft: // 反撇号
        actionEditTime();
        return ;
    case Qt::Key_Apostrophe: // 这是单引号……
        break;
    }

    QScrollArea::keyPressEvent(event);
}

TimelineBucket *TimelineWidget::createItemWidget(QString time, QStringList texts)
{
    TimelineBucket* bucket = new TimelineBucket(center_widget);
    bucket->setTime(time);
    bucket->setText(texts);
    connect(bucket, SIGNAL(signalBucketContentsChanged()), this, SIGNAL(targetItemsChanged()));
    return bucket;
}

void TimelineWidget::updateUI()
{
    QString style = "#TimelineTimeLabel { background:white; border: 1px solid orange; border-radius: 5px; padding: 10px; }"
            "#TimelineTextLabel { background:white; border: 1px solid blue; border-radius: 5px; padding: 10px; }"
            "#TimelineEdit { background:white; border: 1px solid transparent; border-radius: 5px; padding: 5px; margin: 1px; margin-left: 4px;}";
    setStyleSheet(style);
}

void TimelineWidget::slotBucketWidgetToSelect(TimelineBucket *bucket)
{
    finishEditing();

    if (QApplication::keyboardModifiers() == Qt::NoModifier) // 没有修饰符，单选
    {
        setCurrentItem(bucket);
    }
    else if (QApplication::keyboardModifiers() == Qt::ControlModifier) // 按下 ctrl
    {
        if (!bucket->isSelected())
            setCurrentItem(bucket, true);
        else
            unselectItem(bucket);
    }
    else if (QApplication::keyboardModifiers() == Qt::ShiftModifier) // 按下 shift
    {
        int prev = current_index; // 上次按下的
        int curr = buckets.indexOf(bucket);
        if (prev != -1)
        {
            if (prev < curr)
            {
                // 判断是否已经全选
                bool has_unselect = false;
                for (int i = prev; i <= curr; i++)
                {
                    if (!buckets.at(i)->isSelected())
                    {
                        has_unselect = true;
                        break;
                    }
                }

                // 再次遍历，如果有没有选择的，则选择；否则取消选择
                for (int i = prev; i <= curr; i++)
                {
                    TimelineBucket* bucket = buckets[i];
                    if (bucket->isSelected() != has_unselect)
                    {
                        selected_buckets.append(bucket);
                        bucket->setSelected(has_unselect);
                    }
                }
            }
            else if (prev > curr)
            {
                bool has_unselect = false;
                for (int i = prev; i >= curr; i--)
                {
                    if (!buckets.at(i)->isSelected())
                    {
                        has_unselect = true;
                        break;
                    }
                }

                for (int i = prev; i >= curr; i--)
                {
                    TimelineBucket* bucket = buckets[i];
                    if (bucket->isSelected() != has_unselect)
                    {
                        selected_buckets.append(bucket);
                        bucket->setSelected(has_unselect);
                    }
                }
            }
        }
        current_index = curr;
    }
    emit manualSelected();
}

void TimelineWidget::slotTimeWidgetClicked(TimelineTimeLabel *label)
{

}

void TimelineWidget::slotTextWidgetClicked(TimelineTextLabel *label)
{

}

void TimelineWidget::slotTimeWidgetDoubleClicked(TimelineTimeLabel *label)
{
    QTimer::singleShot(0, [=]{
        editing_bucket = buckets.at(current_index);
        editing_label = label;
        edit->move(label->pos() + label->parentWidget()->pos());
        edit->setPlainText(label->text());
        edit->resize(label->size());
        edit->setOriginText(label->text());
        edit->show();
        edit->raise();
        edit->setFocus();
        edit->selectAll();
    });
}

void TimelineWidget::slotTextWidgetDoubleClicked(TimelineTextLabel *label)
{
    QTimer::singleShot(0, [=]{
        editing_bucket = buckets.at(current_index);
        editing_label = label;
        edit->move(label->pos() + label->parentWidget()->pos());
        edit->setPlainText(label->text());
        edit->resize(label->size());
        edit->setOriginText(label->text());
        edit->show();
        edit->raise();
        edit->setFocus();
        edit->selectAll();
    });
}

void TimelineWidget::slotMenuShowed(const QPoint &pos)
{
    QMenu* menu = new QMenu("菜单", this);
    QAction* add_text_action = new QAction("添加文字节点", this);
    QAction* add_line_action = new QAction("添加新行", this);
    QAction* insert_above_action = new QAction("上方插入行", this);
    QAction* insert_under_action = new QAction("下方插入行", this);
    QAction* delete_line_action = new QAction("删除行", this);
    QAction* copy_text_action = new QAction("复制文字", this);
    QAction* paste_action = new QAction("剪贴板导入", this);
    menu->addAction(add_text_action);
    menu->addAction(add_line_action);
    menu->addAction(insert_above_action);
    menu->addAction(insert_under_action);
    menu->addAction(delete_line_action);
    menu->addSeparator();
    menu->addAction(copy_text_action);
    menu->addAction(paste_action);

    if (current_index == -1)
    {
        add_text_action->setEnabled(false);
        insert_above_action->setEnabled(false);
        insert_under_action->setEnabled(false);
        delete_line_action->setEnabled(false);
        copy_text_action->setEnabled(false);
    }

    // 设置事件
    connect(add_text_action, SIGNAL(triggered()), this, SLOT(actionAddText()));
    connect(add_line_action, SIGNAL(triggered()), this, SLOT(actionAddLine()));
    connect(insert_above_action, SIGNAL(triggered()), this, SLOT(actionInsertAbove()));
    connect(insert_under_action, SIGNAL(triggered()), this, SLOT(actionInsertUnder()));
    connect(delete_line_action, SIGNAL(triggered()), this, SLOT(actionDeleteLine()));
    connect(copy_text_action, SIGNAL(triggered()), this, SLOT(actionCopyText()));
    connect(paste_action, SIGNAL(triggered()), this, SLOT(actionPaste()));

    menu->exec(QCursor::pos());
}

void TimelineWidget::slotDroppedAndMoved(TimelineBucket *from, TimelineBucket *to)
{
    int from_index = buckets.indexOf(from);
    int to_index = buckets.indexOf(to);
    timeline_undos->moveCommand(from_index, to_index);
}

void TimelineWidget::slotEditChanged()
{

}

/**
 * 编辑某一个节点
 * @param row 时间行
 * @param col 改行第几项。0为时间，1开始为文字
 */
void TimelineWidget::slotEdit(int row, int col)
{
    if (row < 0 || row >= buckets.size())
        return;
    auto bucket = buckets.at(row);
    bucket->edit(col);
}

/**
 * 准备进行其他操作时，如果正在编辑，则结束编辑
 */
void TimelineWidget::finishEditing()
{
    edit->finishIfEditing();
}

void TimelineWidget::actionAddText()
{
    QList<int> bucket_indexes = selectedIndexes();
    QList<QList<int>> texts_indexes;
    foreach (auto bucket_index, bucket_indexes)
    {
        texts_indexes << QList<int>{buckets.at(bucket_index)->count()};
    }

    timeline_undos->addCommand(bucket_indexes, texts_indexes);

    if (bucket_indexes.size() == 1)
    {
        // 等待动画结束，显示编辑框
        QTimer::singleShot(300, [=]{
            slotEdit(bucket_indexes.first(), buckets.at(bucket_indexes.first())->count());
        });
    }
}

void TimelineWidget::actionAddTextLeft()
{

}

void TimelineWidget::actionAddTextRight()
{

}

void TimelineWidget::actionEditTime()
{
    if (current_index == -1)
        return ;
    auto bucket = at(current_index);
    slotTimeWidgetDoubleClicked(bucket->timeLabel());
}

void TimelineWidget::actionEditText(int index)
{
    if (current_index == -1)
        return ;
    auto bucket = at(current_index);
    if (bucket->count() <= index)
        return ;
    slotTextWidgetDoubleClicked(bucket->at(index));
}

void TimelineWidget::actionAddLine()
{
    timeline_undos->addCommand(count());
    setCurrentItem(count()-1);
    scrollTo();
}

void TimelineWidget::actionInsertAbove()
{
    QList<int> indexes = selectedIndexes();

    timeline_undos->addCommand(indexes);

    unselectAll();
    int cumu = 0;
    for (int i = 0; i < indexes.count(); i++)
    {
        auto bucket = buckets.at(indexes.at(i)+cumu);
        bucket->setSelected(true);
        selected_buckets.append(bucket);
        cumu++;
    }
}

void TimelineWidget::actionInsertUnder()
{
    QList<int> indexes = selectedIndexes(1);

    timeline_undos->addCommand(indexes);

    unselectAll();
    int cumu = 0;
    for (int i = 0; i < indexes.count(); i++)
    {
        auto bucket = buckets.at(indexes.at(i)+cumu);
        bucket->setSelected(true);
        selected_buckets.append(bucket);
        cumu++;
    }
}

void TimelineWidget::actionDeleteLine()
{
    finishEditing();

    QList<int> indexes = selectedIndexes();

    timeline_undos->deleteCommand(indexes);
    selected_buckets.clear();
    current_index = -1;
}

void TimelineWidget::actionCopyText()
{
    QString result;
    foreach (auto bucket, buckets)
    {
        if (bucket->isSelected())
        {
            if (!result.isEmpty())
                result += "\n";
            result += bucket->toString();
        }
    }
    QApplication::clipboard()->setText(result);
}

void TimelineWidget::actionPaste()
{
    QString text = QApplication::clipboard()->text();
    if (text.isEmpty())
        return ;
    int c = this->count();
    fromString(text);
    int c2 = this->count();
    if (c == c2) // 没有变化
        return ;
    unselectAll();
    for (int i = c; i < c2; i++)
        selectItem(at(i));
    scrollTo(c2-1);
    scrollTo(c);
}
