#pragma once

#include <QtNetwork/QTcpServer>
#include <QtWidgets/QDialog>
#include <QtWidgets/QLabel>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QPushButton>

namespace motive::ui {

class LoginDialog : public QDialog
{
public:
    explicit LoginDialog(const QString& apiBaseUrl, QWidget* parent = nullptr);

    QString token() const;
    QString email() const;
    QString licenseKey() const;

    static QString storedToken();
    static QString storedEmail();
    static QString storedLicenseKey();
    static void storeCredentials(const QString& token, const QString& email, const QString& licenseKey);
    static void clearCredentials();
    static bool hasStoredCredentials();

    static bool signInWithBrowser(const QString& apiBaseUrl,
                                  QWidget* parent = nullptr,
                                  const QString& preferredProvider = QStringLiteral("google"),
                                  QString* outError = nullptr);

private:
    void setStatus(const QString& msg, bool isError = false);
    void setFieldsEnabled(bool enabled);
    void doAuthRequest(const QString& endpoint);
    void startOAuthFlow(const QString& provider);
    void onLoginClicked();
    void onRegisterClicked();
    void onSkipClicked();
    void switchToRegister();
    void switchToLogin();

    QString apiBaseUrl_;
    QLineEdit* emailEdit_ = nullptr;
    QLineEdit* passwordEdit_ = nullptr;
    QLineEdit* passwordConfirmEdit_ = nullptr;
    QLabel* statusLabel_ = nullptr;
    QLabel* titleLabel_ = nullptr;
    QPushButton* actionButton_ = nullptr;
    QPushButton* switchButton_ = nullptr;
    QPushButton* skipButton_ = nullptr;
    QWidget* confirmRow_ = nullptr;
    bool isRegisterMode_ = false;
    QTcpServer* oauthServer_ = nullptr;

    QString token_;
    QString email_;
    QString licenseKey_;
};

}  // namespace motive::ui
