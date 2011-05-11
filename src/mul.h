/*
 * Copyright (c) 2010
 * Petr Mrázek     (xmraze03@stud.fit.vutbr.cz),
 * Miroslav Dvořák (xdvora11@stud.fit.vutbr.cz)
 */

#ifndef mul_H
#define mul_H

#include <QtGui/QMainWindow>
#include "ui_main.h"
#include <QtMultimedia>
#include <QDir>
#include <QFile>

enum RecorderState
{
    inactive,        // nothing going on
    listening,       // we are listening on the device, waiting for the initial spike
    recording,       // we are recording and listening for the ending spike
    finished_record, // there's a finished recording waiting to be taken from us
    encoding_wav,    // saving wav files
    encoding_mp3,    // saving mp3 files
    finished_process, // finalized!
};

struct work_data
{
    bool do_mp3;
    bool do_normalize;
    bool do_trim;
    bool do_split;
    QDir tempDir;
    QDir outDir;
};

class work_order
{
public:
    work_order(work_data& data, int index)
    {
        state = inactive;
        record_index = index;
        this->data = data;
        file_index = 0;
        currentFile = 0;
        current_file_length = 0;
    };
    // make a new temp file
    void mkFile();
    // close last temp file
    void finFile();
    // push data into the temp files
    void push_data(const char* data, qint64 len);
    bool do_process ();
    RecorderState state;
    // list of files used for storing temporary data
    QList <QFile *> recfiles;
    work_data data;
    int record_index; // current recording index (for names)
    int file_index;   // current file of the recording (for names)
    QFile * currentFile;
    quint64 current_file_length;
};

class Worker : public QObject
{
    Q_OBJECT
    
public:
    explicit Worker( int id_, QObject *parent = 0 ) : QObject(parent)
    {
        moveToThread(&m_thread);
        id = id_;
        m_thread.start();
    }

    ~Worker()
    {
        // Gracefull thread termination (queued in exec loop)
        if( m_thread.isRunning() )
        {
            m_thread.quit();
            m_thread.wait();
        }
    }
public slots:
    void doTask( work_order * param )
    {
        QMetaObject::invokeMethod( this, "doTaskImpl", Q_ARG( work_order *, param ) );
    }
private slots:
    void doTaskImpl( work_order * param );
    
signals:
    void postTaskState(int index, RecorderState s);
    void finished(int id);
    
private:
    QThread m_thread;
    int id;
};

// dumps audo data into files.
class Recorder : public QIODevice
{
    Q_OBJECT
public:
    Recorder(const QAudioFormat &format, QObject *parent);
    ~Recorder();
    void start(int rec_idx, work_data data);
    int stop(); // returns first unused rec_idx

    qint64 readData(char *data, qint64 maxlen);
    qint64 writeData(const char *data, qint64 len);

private:
    work_order * mkWorkOrder();
    void startRecording();
    void finishRecording(bool listen);
    void stateChange(RecorderState newstate);
    work_data wdata;  // template for work orders
    QList <work_order*> tasks;
    work_order * current_task;
    const QAudioFormat m_format;
    int next_record_idx;
    // detekce lupancu
    quint32 detector;
    quint32 delay;
signals:
    void update();
    void postRecorderState(int index, RecorderState s);
    void finished(work_order * ord);
};

class mul : public QMainWindow
{
    Q_OBJECT
public:
    mul(QWidget *parent = 0);
    virtual ~mul();
    virtual void closeEvent(QCloseEvent* );
private:
    bool startRecord();
    bool endRecord();
    bool setAudioDevice(int index);
    Ui::MainWindow ui;
    QString tempPath;
    QString outPath;
    QList<QAudioDeviceInfo> devs;

    QAudioDeviceInfo m_device;
    Recorder *m_recorder;
    QMap<int, Worker *> workers;
    int active_workers;

    QAudioFormat m_format;
    QAudioInput *m_audioInput;
    QIODevice *m_input;
    QByteArray m_buffer;
    work_data current_w_data;
    int last_record_idx;
    int last_worker_id;
public slots:
    void notified();
    void stateChanged(QAudio::State state);
    void recorderStatus(int index, RecorderState state);
    void recorderFinished(work_order*);
    void workerFinished(int id);

    void slotPickTemp(bool);
    void slotPickOutput(bool);
    void slotArm(bool);
    void slotFlipMp3(bool);
    void slotFlipNormalize(bool);
    void slotFlipTrim(bool);
    void slotFlipSplit(bool);
    void slotDeviceChange(int index);
};
#endif // mul_H
