#ifndef UISTATEMANAGER_H
#define UISTATEMANAGER_H

#include <QObject>

class QTableView;
class QStandardItemModel;
class SimplePaginationModel;
class QLabel;
class QProgressBar;

/**
 * @brief UI状态管理器
 * 
 * 职责：
 * 1. 管理表格选择状态
 * 2. 管理搜索模式状态
 * 3. 管理分页/非分页模式切换
 * 4. 统一UI更新逻辑
 * 
 * 目的：将UI状态管理从MainWindow中分离，符合单一职责原则
 */
class UIStateManager : public QObject
{
    Q_OBJECT

public:
    explicit UIStateManager(QObject *parent = nullptr);

    // 选择状态管理
    int getCurrentSelectedRow() const { return m_currentSelectedRow; }
    void setCurrentSelectedRow(int row);
    void clearSelection();
    bool hasSelection() const { return m_currentSelectedRow >= 0; }

    // 记录操作后的选择状态
    void markShouldSelectNewRecord();
    void markShouldSelectEditedRecord(int row);
    void clearSelectionMarks();

    // 分页模式管理
    bool isPaginationMode() const { return m_usePagination; }
    void setPaginationMode(bool enabled);

    // 搜索模式管理
    bool isSearchMode() const { return m_isSearchMode; }
    void setSearchMode(bool enabled);

    // UI更新
    void updateStatusBar(QLabel *statusLabel, int totalRecords, double totalBudget, const QStringList &statusStats);
    void updateProgressBar(QProgressBar *progressBar, bool visible, int value = -1, int maximum = 0);

signals:
    void selectionChanged(int row);
    void modeChanged(bool paginationMode);
    void needRefreshSelection();  // 需要刷新选择状态

public:
    // 允许外部访问选择标记（用于数据加载完成后的选择处理）
    bool m_shouldSelectNewRecord;
    bool m_shouldSelectEditedRecord;
    int m_editedRecordRow;

private:
    // 选择状态
    int m_currentSelectedRow;

    // 模式状态
    bool m_usePagination;
    bool m_isSearchMode;
};

#endif // UISTATEMANAGER_H
