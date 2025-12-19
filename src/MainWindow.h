#pragma once

#include <QMainWindow>
#include <QMediaPlayer>
#include <QSet>
#include <QVector>

class QAudioOutput;
class QLineEdit;
class QListView;
class QPushButton;
class QSortFilterProxyModel;
class QStandardItemModel;
class QMediaPlayer;
class QLabel;
class QSlider;
class QToolButton;

class MainWindow final : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

private slots:
    void addFolder();
    void playSelected();
    void playNext();
    void playPrevious();
    void playPause();
    void stop();
    void updatePosition(qint64 position);
    void updateDuration(qint64 duration);
    void seek(int value);
    void onSearchTextChanged(const QString &text);
    void updatePlayState();
    void updateSelectionLabel(const QModelIndex &current);
    void handleMediaStatus(QMediaPlayer::MediaStatus status);
    void toggleShuffle();
    void cycleRepeat();
    void updateVolume(int value);

private:
    void setupUi();
    void scanFolder(const QString &path);
    void addTrack(const QString &filePath);
    void playTrack(const QString &filePath, bool recordHistory = true);
    void playIndex(const QModelIndex &proxyIndex);
    void updateCounts();
    QString formatTime(qint64 ms) const;

    QLineEdit *searchEdit_ = nullptr;
    QListView *listView_ = nullptr;
    QPushButton *playPauseButton_ = nullptr;
    QPushButton *stopButton_ = nullptr;
    QToolButton *prevButton_ = nullptr;
    QToolButton *nextButton_ = nullptr;
    QToolButton *shuffleButton_ = nullptr;
    QToolButton *repeatButton_ = nullptr;
    QToolButton *addFolderButton_ = nullptr;
    QLabel *coverLabel_ = nullptr;
    QLabel *nowPlayingTitleLabel_ = nullptr;
    QLabel *nowPlayingPathLabel_ = nullptr;
    QLabel *timeLabel_ = nullptr;
    QSlider *seekSlider_ = nullptr;
    QSlider *volumeSlider_ = nullptr;
    QLabel *countLabel_ = nullptr;

    QStandardItemModel *model_ = nullptr;
    QSortFilterProxyModel *filter_ = nullptr;

    QMediaPlayer *player_ = nullptr;
    QAudioOutput *audioOutput_ = nullptr;
    bool isPlaying_ = false;
    qint64 durationMs_ = 0;
    bool shuffleEnabled_ = false;
    int repeatMode_ = 0;
    QString currentFilePath_;
    QVector<QString> playHistory_;
    QSet<QString> trackSet_;
};
