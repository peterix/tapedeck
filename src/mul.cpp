/*
 * Copyright (c) 2010
 * Petr Mrázek     (xmraze03@stud.fit.vutbr.cz),
 * Miroslav Dvořák (xdvora11@stud.fit.vutbr.cz)
 */

#include "mul.h"
#include <QFileDialog>
#include <QDebug>
#include <QCloseEvent>
#include <QMessageBox>
#include <QtMultimedia>
#include <QThread>
#include <sndfile.hh>
#include <lame/lame.h>
#include <stdio.h>

// finalize file
void work_order::finFile()
{
    if(currentFile)
    {
        // close file and put it into the list of finished files
        currentFile->close();
        recfiles.append(currentFile);
        currentFile = 0; // we have no current file anymore
        file_index++;
    }
}

// make new, fresh file
void work_order::mkFile()
{
    if(!currentFile)
    {
        QString filePath =
        QDir::fromNativeSeparators(
            data.tempDir.absoluteFilePath(
                QString("MULtemp_")
                + QString::number(record_index)
                + QString("_")
                + QString::number(file_index)
                + QString(".dat")
            )
        );
        qWarning() << filePath << endl;
        currentFile = new QFile( filePath );
        currentFile->open(QFile::WriteOnly);
        current_file_length = 0;
    }
}

void work_order::push_data(const char* data, qint64 len)
{
    quint64 start = 0;
    quint64 proclen = len;
    while (proclen)
    {
        quint64 file_space = (1024 * 1024) - current_file_length;
        quint64 can_write = qMin<quint64>(file_space, proclen);
        currentFile->write(data + start, can_write);
        current_file_length += can_write;
        proclen -= can_write;
        start += can_write;
        if(proclen)
        {
            // make me another file dammit!
            finFile();
            mkFile();
        }
    }
}

// do all the work!
void Worker::doTaskImpl( work_order * param )
{
    if(param->data.do_mp3)
    {
        emit postTaskState( param->record_index, encoding_mp3 );
        QString filePath = param->data.outDir.absoluteFilePath
        (
            QString::fromUtf8("Záznam ")
            + QString::number(param->record_index)
            + QString(".mp3")
        );
        QFile MP3(filePath);
        if(!MP3.open(QFile::WriteOnly))
        {
            // TODO: error check here
        }
        int read, write;
        const int PCM_SIZE = 8192;
        const int MP3_SIZE = 8192;

        unsigned char mp3_buffer[MP3_SIZE];

        lame_t lame = lame_init();
        lame_set_in_samplerate(lame, 44100);
        //lame_set_num_channels(lame,1);
        lame_set_mode(lame,MONO);
        lame_set_VBR(lame, vbr_default);
        lame_init_params(lame);
        foreach(QFile * f, param->recfiles )
        {
            f->open(QFile::ReadOnly);
            do {
                QByteArray dataz = f->read(PCM_SIZE * 2);
                short int * pcm_buffer = (short int *) dataz.data();
                read = dataz.size()/2;
                if (!dataz.size())
                {
                    // reached end of file, continue with next one
                    break;
                }
                else
                    write = lame_encode_buffer(lame, pcm_buffer,pcm_buffer, read, mp3_buffer, MP3_SIZE);
                MP3.write( ( const char *)mp3_buffer,write);
            } while (read != 0);
            f->close();
            f->remove(); // delete temp
        }
        // flush stuffs
        write = lame_encode_flush(lame, mp3_buffer, MP3_SIZE);
        MP3.write((const char *)mp3_buffer,write);
        lame_close(lame);
        MP3.close();
    }
    else
    {
        emit postTaskState( param->record_index, encoding_wav );
        QString filePath = param->data.outDir.absoluteFilePath
        (
            QString::fromUtf8("Záznam ")
            + QString::number(param->record_index)
            + QString(".wav")
        );
        QByteArray fnameBA = filePath.toLocal8Bit();
        qWarning() << filePath;
        const int format=SF_FORMAT_WAV | SF_FORMAT_PCM_16;
        const int channels=1;
        const int sampleRate=44100;
        SndfileHandle outfile(fnameBA.data(), SFM_WRITE, format, channels, sampleRate);
        if (outfile.error())
        {
            qWarning() << "unable to write file" << filePath;
            qWarning() << outfile.strError();
            return;
        }
        foreach(QFile * f, param->recfiles )
        {
            f->open(QFile::ReadOnly);
            QByteArray data = f->readAll();
            outfile.write( (const short int *)data.data(), data.size()/2);
            f->close();
            f->remove(); // delete temp
        }
    }
    emit postTaskState( param->record_index, finished_process );
}

Recorder::Recorder(const QAudioFormat &format, QObject *parent)
    :   QIODevice(parent)
    ,   m_format(format)
{
    current_task = 0;
}

Recorder::~Recorder()
{
}

work_order* Recorder::mkWorkOrder()
{
    work_order * ord = new work_order(wdata, next_record_idx);
    tasks.append(ord);
    current_task = ord;
    next_record_idx ++;
    stateChange(listening);
    return ord;
}

void Recorder::start(int rec_idx, work_data data)
{
    next_record_idx = rec_idx;
    wdata = data;
    // create a fresh, new work order
    mkWorkOrder();
    open(QIODevice::WriteOnly);
    detector = 0;
    delay = 0;
}
void Recorder::stateChange(RecorderState newstate)
{
    if(!current_task)
        return;
    if(current_task->state != newstate)
    {
        current_task->state = newstate;
        emit postRecorderState(current_task->record_index, newstate);
    }
}

int Recorder::stop()
{
    close();
    switch(current_task->state)
    {
        case inactive:
            // do nothing
            break;
        case listening:
            // we were listening, we become inactive.
            stateChange(inactive);
            break;
        case recording:
            finishRecording(0);
            break;
            // finishing record
        case finished_record:
            // there is a recording waiting... this shouldn't happen
            break;
    }
    return next_record_idx;
}

void Recorder::startRecording()
{
    if( current_task->state == listening)
    {
        current_task->file_index = 0;
        current_task->mkFile();
        stateChange(recording);
    }
}

void Recorder::finishRecording(bool listen)
{
    //just checking ;)
    if(current_task->state == recording)
    {
        // finalize current work order and send it to the listener
        current_task->finFile();
        stateChange(finished_record);
        emit finished( current_task );
        current_task = 0;
        if(listen)
        {
            // make a new work order
            mkWorkOrder();
        }
    }
}

qint64 Recorder::readData(char *data, qint64 maxlen)
{
    Q_UNUSED(data)
    Q_UNUSED(maxlen)

    return 0;
}
// data enters our domain here
qint64 Recorder::writeData(const char *data, qint64 len)
{
    Q_ASSERT(m_format.sampleSize() % 8 == 0);
    const int channelBytes = m_format.sampleSize() / 8;
    const int sampleBytes = m_format.channels() * channelBytes;
    Q_ASSERT(len % sampleBytes == 0);
    const int numSamples = len / sampleBytes;
    const unsigned char *ptr = (const unsigned char *)(data);
    int i = 0;
    Q_ASSERT(current_task);
    switch(current_task->state)
    {
        case inactive:
            qWarning() << "inactive Recorder is getting data!" << endl;
            break;
        case listening:
            for (; i < numSamples; ++i)
            {
                qint16 value = qFromLittleEndian<qint16>(ptr);
                if(delay) delay --;
                else
                {
                    if(value > 12000)
                        detector ++;
                    else detector = 0;
                    if(detector == 500)
                    {
                        startRecording();
                        delay = 3*44100;
                        detector = 0;
                        break;
                    }
                }
                ptr += channelBytes;
            }
            // listening for a bump
            
            break;
        case recording:
            for (; i < numSamples; ++i)
            {
                qint16 value = qFromLittleEndian<qint16>(ptr);
                if(delay) delay --;
                else
                {
                    if(value > 12000)
                        detector ++;
                    else detector = 0;
                    if(detector == 500)
                    {
                        finishRecording(1);
                        delay = 3*44100;
                        detector = 0;
                        break;
                    }
                }
                ptr += channelBytes;
            }
            break;
        case finished_record:
            qWarning() << "inattentive Recorder is getting data!" << endl;
            break;
    }
    
    if(current_task->state == recording)
    {
        current_task->push_data(data, len);
    }
    emit update();
    return len;
}

// otevre explorer/nautilus/dolphin/finder/etc... s uvedenou cestou
void openNativeFileBrowser(QString raw_path)
{
    QString path = QDir::toNativeSeparators(raw_path);
    QDesktopServices::openUrl(QUrl("file:///" + path));
}

mul::mul(QWidget *parent): QMainWindow(parent)
{
    m_recorder = 0;
    m_audioInput = 0;
    last_record_idx = 1;
    active_workers = 0;
    ui.setupUi(this);
    // velke cervene tlacitko
    connect(ui.ARMButton,SIGNAL(toggled(bool)),this,SLOT(slotArm(bool)));
    // prochazeni slozek
    connect(ui.workDirBrowse,SIGNAL(clicked(bool)),this,SLOT(slotPickTemp(bool)));
    connect(ui.outDirBrowse,SIGNAL(clicked(bool)),this,SLOT(slotPickOutput(bool)));
    qRegisterMetaType<work_order *>("work_order *");
    qRegisterMetaType<RecorderState>("RecorderState");
    /*
     * checkboxy
     */
    // mp3 nebo wav?
    current_w_data.do_mp3 = ui.checkMP3->isChecked();
    connect(ui.checkMP3,SIGNAL(toggled(bool)),this,SLOT(slotFlipMp3(bool)));
    // normalizovat?
    current_w_data.do_normalize = ui.checkNormalize->isChecked();
    connect(ui.checkNormalize,SIGNAL(toggled(bool)),this,SLOT(slotFlipNormalize(bool)));
    // rozdelit?
    current_w_data.do_split = ui.checkDivide->isChecked();
    connect(ui.checkDivide,SIGNAL(toggled(bool)),this,SLOT(slotFlipSplit(bool)));
    // orezat ticho
    current_w_data.do_trim = ui.checkTrim->isChecked();
    connect(ui.checkTrim,SIGNAL(toggled(bool)),this,SLOT(slotFlipTrim(bool)));

    // vyber zarizeni
    connect(ui.devicesCBox,SIGNAL(currentIndexChanged(int)),this,SLOT(slotDeviceChange(int)));

    current_w_data.tempDir = QDir::temp();
    tempPath = QDir::tempPath();
    ui.workDirText->setText(tempPath);
    current_w_data.outDir = QDir::home();
    outPath = QDir::homePath();
    ui.outDirText->setText(outPath);
    QAudio::Mode m;

    /*
     * audio veci
     */
    // rozumny format
    m_format.setFrequency(44100);
    m_format.setChannels(1);
    m_format.setSampleSize(16);
    m_format.setSampleType(QAudioFormat::SignedInt);
    m_format.setByteOrder(QAudioFormat::LittleEndian);
    m_format.setCodec("audio/pcm");
    // init the device list, make sure devices support our required format!
    devs = QAudioDeviceInfo::availableDevices(QAudio::AudioInput);
    for(int i = 0; i < devs.size(); ++i)
    {
        if(!devs.at(i).isFormatSupported(m_format))
            continue;
        ui.devicesCBox->addItem(devs.at(i).deviceName(), qVariantFromValue(devs.at(i)));
    }
    // no valid inputs -> we fail
    if(!ui.devicesCBox->count())
    {
        return;
    }
    setAudioDevice(ui.devicesCBox->currentIndex());
}

mul::~mul()
{}
void mul::closeEvent(QCloseEvent* e)
{
    //TODO: check state of work order processing
    if(ui.ARMButton->isChecked())
    {
        QMessageBox::warning
        (
            this,
            QString::fromUtf8("Upozornění"),
            QString::fromUtf8("Nejdřív ukončete záznam.")
        );
        e->ignore();
    }
    else
    {
        // TODO: uklid pred vynucenym ukoncenim sem.
        e->accept();
    }
}

bool mul::startRecord()
{
    if(m_recorder && m_audioInput)
    {
        m_recorder->start(last_record_idx,current_w_data);
        m_audioInput->start(m_recorder);
        return true;
    }
    else
        return false;
}
bool mul::endRecord()
{
    if(m_recorder && m_audioInput)
    {
        m_audioInput->stop();
        last_record_idx = m_recorder->stop();
        return true;
    }
    else
        return false;
}
bool mul::setAudioDevice(int index)
{
    if(m_recorder)
    {
        m_recorder->disconnect();
    }
    else
    {
        m_recorder  = new Recorder(m_format, this);
    }
    if(m_audioInput)
    {
        m_audioInput->disconnect();
        delete m_audioInput;
    }
    m_device = ui.devicesCBox->itemData(index).value<QAudioDeviceInfo>();
    m_audioInput = new QAudioInput(m_device, m_format, this);
    connect(m_audioInput, SIGNAL(notify()), SLOT(notified()));
    connect(m_audioInput, SIGNAL(stateChanged(QAudio::State)), SLOT(stateChanged(QAudio::State)));
    connect(m_recorder, SIGNAL(finished(work_order*)),this,SLOT(recorderFinished(work_order*)));
    connect(m_recorder, SIGNAL(postRecorderState(int,RecorderState)),this,SLOT(recorderStatus(int,RecorderState)));
}

void mul::slotPickOutput(bool )
{
    QString newpath;
    newpath = QFileDialog::getExistingDirectory
    (
        this,
        QString::fromUtf8("Vyberte adresář pro výstup."),
        QDir::homePath(),
        QFileDialog::ShowDirsOnly
    );
    if(!newpath.size())
        return;
    QDir newdir(newpath);
    if(newdir.exists())
    {
        outPath = newpath;
        ui.outDirText->setText(newpath);
        current_w_data.outDir = newdir;
    }
}

void mul::slotPickTemp(bool )
{
    QString newpath;
    newpath = QFileDialog::getExistingDirectory
    (
        this,
        QString::fromUtf8("Vyberte adresář pro dočasné úložiště."),
        QDir::tempPath(),
        QFileDialog::ShowDirsOnly
    );
    if(!newpath.size())
        return;
    QDir newdir(newpath);
    if(newdir.exists())
    {
        tempPath = newpath;
        ui.workDirText->setText(newpath);
        current_w_data.tempDir = newdir;
    }
}

void mul::slotFlipMp3(bool status)
{
    current_w_data.do_mp3 = status;
}
void mul::slotFlipNormalize(bool status)
{
    current_w_data.do_normalize = status;
}
void mul::slotFlipSplit(bool status)
{
    current_w_data.do_split = status;
}
void mul::slotFlipTrim(bool status)
{
    current_w_data.do_trim = status;
}

void mul::slotArm(bool status)
{
    if(status)
    {
        // disable controls here
        ui.checkDivide->setDisabled(1);
        ui.checkMP3->setDisabled(1);
        ui.checkNormalize->setDisabled(1);
        ui.checkTrim->setDisabled(1);
        ui.devicesCBox->setDisabled(1);
        ui.MP3Button->setDisabled(1);
        ui.outDirBrowse->setDisabled(1);
        ui.outDirText->setDisabled(1);
        ui.workDirBrowse->setDisabled(1);
        ui.workDirText->setDisabled(1);
        startRecord();
    }
    else
    {
        // enable controls here
        ui.checkDivide->setDisabled(1); // FIXME: implement
        ui.checkMP3->setDisabled(0);
        ui.checkNormalize->setDisabled(1);
        ui.checkTrim->setDisabled(1);
        ui.devicesCBox->setDisabled(0);
        ui.MP3Button->setDisabled(0);
        ui.outDirBrowse->setDisabled(0);
        ui.outDirText->setDisabled(0);
        ui.workDirBrowse->setDisabled(0);
        ui.workDirText->setDisabled(0);
        endRecord();
    }
}

void mul::notified()
{
    qWarning() << "bytesReady = " << m_audioInput->bytesReady()
    << ", " << "elapsedUSecs = " <<m_audioInput->elapsedUSecs()
    << ", " << "processedUSecs = "<<m_audioInput->processedUSecs();
}
void mul::recorderFinished(work_order * ord)
{
    qWarning() << "Received recording #" << ord->record_index;
    for(int i = 0; i < ord->recfiles.size();i++)
    {
        qWarning() << ord->recfiles[i]->fileName() << endl;
    }
    Worker * w = new Worker(ord->record_index);
    workers[ord->record_index] = w;
    connect(w,SIGNAL(postTaskState(int,RecorderState)),this,SLOT(recorderStatus(int,RecorderState)));
    connect(w,SIGNAL(finished(int)),this,SLOT(workerFinished(int)));
    active_workers++;
    w->doTask(ord);
}

void mul::workerFinished( int id )
{
    Worker * w = workers.take(id);
    qWarning() << "Killing worker " << id;
    active_workers --;
    delete w;
}

void mul::recorderStatus(int index, RecorderState state)
{
    qWarning() << "Received status update #" << index << endl;
    int nrows = ui.seznam->rowCount();
    if (index > nrows)
    {
        ui.seznam->setRowCount(index);
        QTableWidgetItem *newItem = new QTableWidgetItem(QString::fromUtf8("Záznam ") + QString::number(index));
        ui.seznam->setItem(index-1,0, newItem);
    }
    QTableWidgetItem * it_action = ui.seznam->item(index -1, 1);
    if(!it_action)
    {
        it_action = new QTableWidgetItem();
        ui.seznam->setItem(index-1,1, it_action);
    }
    QTableWidgetItem * it_progress = ui.seznam->item(index -1, 2);
    if(!it_progress)
    {
        it_progress = new QTableWidgetItem();
        ui.seznam->setItem(index-1,2, it_progress);
    }
    switch(state)
    {
        case inactive:
            it_action->setText(QString::fromUtf8("Neaktivní"));
            break;
        case listening:
            it_action->setText(QString::fromUtf8("Čeká na signál"));
            break;
        case recording:
            it_action->setText(QString::fromUtf8("Nahrává se"));
            break;
        case finished_record:
            it_action->setText(QString::fromUtf8("Nahrávání hotovo"));
            break;
        case encoding_wav:
            it_action->setText(QString::fromUtf8("Zápis wav"));
            break;
        case encoding_mp3:
            it_action->setText(QString::fromUtf8("Zápis mp3"));
            break;
        case finished_process:
            it_action->setText(QString::fromUtf8("Hotovo"));
            break;
    }
}

void mul::stateChanged(QAudio::State state)
{
    /*
    if(state == QAudio::ActiveState)
    QMessageBox::warning
    (
        this,
        QString::fromUtf8("Upozornění"),
        QString::fromUtf8("ACTIVATING")
    );
    */
}

void mul::slotDeviceChange(int index)
{
    setAudioDevice(index);
}

#include "mul.moc"
