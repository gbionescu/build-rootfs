#include <QApplication>
#include <QElapsedTimer>
#include <QFont>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QShortcut>
#include <QString>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

class StopwatchWindow : public QWidget
{
public:
    explicit StopwatchWindow(QWidget *parent = nullptr);

private:
    void toggleRunning();
    void reset();
    void addLap();
    void refreshDisplay();
    QString formatDuration(qint64 milliseconds) const;

    QLabel *timeLabel;
    QPushButton *startStopButton;
    QPushButton *lapButton;
    QPushButton *resetButton;
    QListWidget *lapList;
    QTimer *refreshTimer;
    QElapsedTimer elapsed;
    qint64 accumulatedMs;
    bool running;
    int lapCount;
};

StopwatchWindow::StopwatchWindow(QWidget *parent)
    : QWidget(parent),
      timeLabel(new QLabel(this)),
      startStopButton(new QPushButton("Start", this)),
      lapButton(new QPushButton("Lap", this)),
      resetButton(new QPushButton("Reset", this)),
      lapList(new QListWidget(this)),
      refreshTimer(new QTimer(this)),
      accumulatedMs(0),
      running(false),
      lapCount(0)
{
    setWindowTitle("Qt Stopwatch");
    setMinimumSize(420, 520);

    QFont timeFont("Monospace", 34, QFont::Bold);
    timeFont.setStyleHint(QFont::Monospace);
    timeLabel->setFont(timeFont);
    timeLabel->setAlignment(Qt::AlignCenter);
    timeLabel->setText(formatDuration(0));

    startStopButton->setObjectName("startStop");

    QHBoxLayout *buttons = new QHBoxLayout;
    buttons->addWidget(startStopButton);
    buttons->addWidget(lapButton);
    buttons->addWidget(resetButton);

    QLabel *lapTitle = new QLabel("Laps", this);
    QFont titleFont = lapTitle->font();
    titleFont.setBold(true);
    lapTitle->setFont(titleFont);

    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->addWidget(timeLabel);
    layout->addLayout(buttons);
    layout->addWidget(lapTitle);
    layout->addWidget(lapList, 1);

    setStyleSheet(
        "QWidget { background-color: #1e1e2e; color: #cdd6f4; }"
        "QLabel { color: #f5e0dc; }"
        "QPushButton { background-color: #313244; color: #cdd6f4;"
        "  border: none; border-radius: 8px; padding: 10px 18px;"
        "  font-size: 16px; }"
        "QPushButton:hover { background-color: #45475a; }"
        "QPushButton:pressed { background-color: #585b70; }"
        "QPushButton:disabled { color: #6c7086; }"
        "QPushButton#startStop { background-color: #a6e3a1; color: #1e1e2e;"
        "  font-weight: bold; }"
        "QPushButton#startStop:hover { background-color: #94d68f; }"
        "QListWidget { background-color: #181825; border: 1px solid #313244;"
        "  border-radius: 8px; font-family: Monospace; font-size: 14px; }");

    refreshTimer->setInterval(30);

    connect(refreshTimer, &QTimer::timeout, this, [this] { refreshDisplay(); });
    connect(startStopButton, &QPushButton::clicked, this, [this] { toggleRunning(); });
    connect(resetButton, &QPushButton::clicked, this, [this] { reset(); });
    connect(lapButton, &QPushButton::clicked, this, [this] { addLap(); });

    QShortcut *spaceShortcut = new QShortcut(QKeySequence(Qt::Key_Space), this);
    QShortcut *resetShortcut = new QShortcut(QKeySequence(Qt::Key_R), this);
    QShortcut *lapShortcut = new QShortcut(QKeySequence(Qt::Key_L), this);
    QShortcut *quitShortcut = new QShortcut(QKeySequence(Qt::Key_Escape), this);
    connect(spaceShortcut, &QShortcut::activated, this, [this] { toggleRunning(); });
    connect(resetShortcut, &QShortcut::activated, this, [this] { reset(); });
    connect(lapShortcut, &QShortcut::activated, this, [this] { addLap(); });
    connect(quitShortcut, &QShortcut::activated, this, [this] { close(); });

    lapButton->setEnabled(false);
}

void StopwatchWindow::toggleRunning()
{
    if (running) {
        accumulatedMs += elapsed.elapsed();
        running = false;
        refreshTimer->stop();
        startStopButton->setText("Start");
        lapButton->setEnabled(false);
    } else {
        elapsed.start();
        running = true;
        refreshTimer->start();
        startStopButton->setText("Stop");
        lapButton->setEnabled(true);
    }

    refreshDisplay();
}

void StopwatchWindow::reset()
{
    refreshTimer->stop();
    running = false;
    accumulatedMs = 0;
    lapCount = 0;
    lapList->clear();
    startStopButton->setText("Start");
    lapButton->setEnabled(false);
    refreshDisplay();
}

void StopwatchWindow::addLap()
{
    qint64 total = accumulatedMs + (running ? elapsed.elapsed() : 0);

    lapCount++;
    lapList->addItem(QString("Lap %1    %2")
                         .arg(lapCount)
                         .arg(formatDuration(total)));
    lapList->scrollToBottom();
}

void StopwatchWindow::refreshDisplay()
{
    qint64 total = accumulatedMs + (running ? elapsed.elapsed() : 0);

    timeLabel->setText(formatDuration(total));
}

QString StopwatchWindow::formatDuration(qint64 milliseconds) const
{
    const qint64 hours = milliseconds / 3600000;
    const qint64 minutes = (milliseconds / 60000) % 60;
    const qint64 seconds = (milliseconds / 1000) % 60;
    const qint64 millis = milliseconds % 1000;

    return QString("%1:%2:%3.%4")
        .arg(hours, 2, 10, QChar('0'))
        .arg(minutes, 2, 10, QChar('0'))
        .arg(seconds, 2, 10, QChar('0'))
        .arg(millis, 3, 10, QChar('0'));
}

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    StopwatchWindow window;

    window.show();
    return app.exec();
}
