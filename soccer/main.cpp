#include <gameplay/GameplayModule.hpp>
#include <ui/StyleSheetManager.hpp>

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <assert.h>
#include <signal.h>

#include <QApplication>
#include <QFile>
#include <QDir>
#include <QDateTime>
#include <QString>
#include <QMessageBox>

#include "MainWindow.hpp"
#include "Configuration.hpp"

using namespace std;

//  we use this to catch Ctrl+C and kill the program
void signal_handler(int signum) { exit(signum); }

void usage(const char* prog) {
    fprintf(stderr, "usage: %s [options...]\n", prog);
    fprintf(stderr, "\t-y:          run as the yellow team\n");
    fprintf(stderr, "\t-b:          run as the blue team\n");
    fprintf(stderr, "\t-c <file>:   specify the configuration file\n");
    fprintf(stderr, "\t-s <seed>:   set random seed (hexadecimal)\n");
    fprintf(stderr,
            "\t-pbk <file>: playbook file name as contained in "
            "'soccer/gameplay/playbooks/'\n");
    fprintf(stderr, "\t-ng:         no goalie\n");
    fprintf(stderr, "\t-sim:        use simulator\n");
    fprintf(stderr, "\t-freq:       specify radio frequency (918 or 916)\n");
    fprintf(stderr, "\t-nolog:      don't write log files\n");
    fprintf(stderr, "\t-noref:      don't use external referee commands\n");
    exit(1);
}

int main(int argc, char* argv[]) {
    printf("Starting Soccer...\n");

    //  register our signal handler
    signal(SIGINT, signal_handler);

    // Seed the large random number generator
    long int seed = 0;
    int fd = open("/dev/random", O_RDONLY);
    if (fd >= 0) {
        if (read(fd, &seed, sizeof(seed)) != sizeof(seed)) {
            fprintf(stderr, "Can't read /dev/random, using zero seed: %m\n");
        }
        close(fd);
    } else {
        fprintf(stderr, "Can't open /dev/random, using zero seed: %m\n");
    }

    QApplication app(argc, argv);

    bool blueTeam = false;
    QString cfgFile;
    vector<const char*> playDirs;
    bool sim = false;
    bool log = true;
    QString radioFreq;
    string playbookFile;
    bool noref = false;

    for (int i = 1; i < argc; ++i) {
        const char* var = argv[i];

        if (strcmp(var, "--help") == 0) {
            usage(argv[0]);
        } else if (strcmp(var, "-y") == 0) {
            blueTeam = false;
        } else if (strcmp(var, "-b") == 0) {
            blueTeam = true;
        } else if (strcmp(var, "-sim") == 0) {
            sim = true;
        } else if (strcmp(var, "-nolog") == 0) {
            log = false;
        } else if (strcmp(var, "-freq") == 0) {
            if (i + 1 >= argc) {
                printf("No radio frequency specified after -freq\n");
                usage(argv[0]);
            }

            i++;
            radioFreq = argv[i];
        } else if (strcmp(var, "-c") == 0) {
            if (i + 1 >= argc) {
                printf("no config file specified after -c\n");
                usage(argv[0]);
            }

            i++;
            cfgFile = argv[i];
        } else if (strcmp(var, "-s") == 0) {
            if (i + 1 >= argc) {
                printf("no seed specified after -s\n");
                usage(argv[0]);
            }

            i++;
            seed = strtol(argv[i], nullptr, 16);
        } else if (strcmp(var, "-pbk") == 0) {
            if (i + 1 >= argc) {
                printf("no playbook file specified after -pbk\n");
                usage(argv[0]);
            }

            playbookFile = argv[++i];
        } else if (strcmp(var, "-noref") == 0) {
            noref = true;
        } else {
            printf("Not a valid flag: %s\n", argv[i]);
            usage(argv[0]);
        }
    }

    printf("Running on %s\n", sim ? "simulation" : "real hardware\n");

    printf("seed %016lx\n", seed);
    srand48(seed);

    // Default config file name
    if (cfgFile.isNull()) {
        cfgFile = ApplicationRunDirectory().filePath(sim ? "soccer-sim.cfg"
                                                         : "soccer-real.cfg");
    }

    std::shared_ptr<Configuration> config =
        Configuration::FromRegisteredConfigurables();

    auto processor = std::make_unique<Processor>(sim);
    processor->blueTeam(blueTeam);
    processor->refereeModule()->useExternalReferee(!noref);

    // Load config file
    QString error;
    if (!config->load(cfgFile, error)) {
        QMessageBox::critical(
            nullptr, "Soccer",
            QString("Can't read initial configuration %1:\n%2")
                .arg(cfgFile, error));
    }

    auto win = std::make_unique<MainWindow>(processor.get());
    win->configuration(config.get());
    win->initialize();

    win->setUseRefChecked(!noref);

    if (!ApplicationRunDirectory().exists("./logs")) {
        cerr << "No ./run/logs/ directory - not writing log file" << endl;
    } else if (!log) {
        cerr << "Not writing log file" << endl;
    } else {
        QString logFile =
            ApplicationRunDirectory().filePath("./logs/") +
            QDateTime::currentDateTime().toString("yyyyMMdd-hhmmss.log");
        if (!processor->openLog(logFile)) {
            printf("Failed to open %s: %m\n", (const char*)logFile.toLatin1());
        }
    }

    if (!radioFreq.isEmpty()) {
        if (radioFreq == "916")
            win->setRadioChannel(RadioChannels::MHz_916);
        else if (radioFreq == "918")
            win->setRadioChannel(RadioChannels::MHz_918);
        else
            printf("Cannot recognize radio frequency : %s\n",
                   radioFreq.toStdString().c_str());
    }

    win->logFileChanged();

    processor->start();

    while (
        !processor
             ->isInitialized()) {  // Wait until processor finishes initializing
    }

    if (playbookFile.size() > 0)
        processor->gameplayModule()->loadPlaybook(playbookFile);

    // Sets the initial stylesheet for the application
    // based on the environment variable "SOCCER_THEME"
    StyleSheetManager::changeStyleSheet(win.get(), QString(getenv("SOCCER_THEME")));

    win->show();

    processor->gameplayModule()->setupUI();

    int ret = app.exec();
    processor->stop();

    return ret;
}
