#include "login_dialog.h"
#include <cppmonetize/MonetizeClient.h>
#include <cppmonetize/OAuthDesktopFlow.h>
#include <cppmonetize/TokenStore.h>

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFont>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QJsonObject>
#include <QScreen>
#include <QStandardPaths>
#include <QTextStream>
#include <QVBoxLayout>

#include <algorithm>

namespace motive::ui {
namespace {

cppmonetize::Result<cppmonetize::OAuthConfig> resolveSupabaseOAuthConfig(const QString& apiBaseUrl)
{
    cppmonetize::OAuthDesktopFlow flow;
    return flow.resolveSupabaseConfig(apiBaseUrl);
}

void fitDialogToScreen(QDialog* dialog, const QSize& requestedSize)
{
    if (!dialog)
    {
        return;
    }

    Qt::WindowFlags flags = dialog->windowFlags();
    flags |= Qt::Window;
    flags |= Qt::WindowCloseButtonHint;
    flags &= ~Qt::WindowContextHelpButtonHint;
    dialog->setWindowFlags(flags);
    dialog->setSizeGripEnabled(true);

    QScreen* screen = dialog->screen();
    if (!screen && dialog->parentWidget())
    {
        screen = dialog->parentWidget()->screen();
    }
    if (!screen)
    {
        screen = QGuiApplication::primaryScreen();
    }
    if (!screen)
    {
        dialog->resize(requestedSize);
        return;
    }

    const QRect available = screen->availableGeometry();
    const int maxW = std::max(320, static_cast<int>(available.width() * 0.92));
    const int maxH = std::max(240, static_cast<int>(available.height() * 0.92));
    dialog->setMaximumSize(maxW, maxH);

    const int minW = std::min(dialog->minimumWidth() > 0 ? dialog->minimumWidth() : 320, maxW);
    const int minH = std::min(dialog->minimumHeight() > 0 ? dialog->minimumHeight() : 240, maxH);
    dialog->setMinimumSize(minW, minH);

    const int width = std::clamp(requestedSize.width(), minW, maxW);
    const int height = std::clamp(requestedSize.height(), minH, maxH);
    dialog->resize(width, height);

    QRect frame = dialog->frameGeometry();
    frame.moveCenter(available.center());
    dialog->move(frame.topLeft());
}

QDir credentialDir()
{
    QDir dir(QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation));
    if (!dir.exists())
    {
        dir.mkpath(QStringLiteral("."));
    }
    return dir;
}

QString credPath(const QString& name)
{
    return credentialDir().filePath(name);
}

std::unique_ptr<cppmonetize::TokenStore> createTokenStore()
{
    cppmonetize::TokenStoreConfig cfg;
    cfg.appName = QStringLiteral("motive_editor");
    cfg.orgName = QStringLiteral("motive");
    cfg.serviceName = QStringLiteral("motive.auth");
    return cppmonetize::createDefaultTokenStore(cfg);
}

cppmonetize::MonetizeClient createMonetizeClient(const QString& apiBaseUrl)
{
    cppmonetize::ClientConfig cfg;
    cfg.apiBaseUrl = apiBaseUrl;
    cfg.clientId = QStringLiteral("motive-editor");
    cfg.telemetryHook = [](const cppmonetize::RequestTelemetryEvent& event) {
        if (event.success) {
            return;
        }
        qWarning().noquote() << "[CPPMonetize][motive3d]"
                             << event.operation
                             << "status=" << event.statusCode
                             << "request_id=" << event.clientRequestId
                             << "message=" << event.message;
    };
    return cppmonetize::MonetizeClient(cfg);
}

QString fetchEmailFromToken(const QString& apiBaseUrl, const QString& token)
{
    if (token.isEmpty())
    {
        return {};
    }

    cppmonetize::MonetizeClient client = createMonetizeClient(apiBaseUrl);
    const auto who = client.whoAmI(token);
    if (!who.hasValue()) {
        return {};
    }
    return who.value().email;
}

}  // namespace

QString LoginDialog::storedToken()
{
    auto store = createTokenStore();
    const auto result = store->loadToken();
    if (!result.hasValue()) {
        return {};
    }
    return result.value().trimmed();
}

QString LoginDialog::storedEmail()
{
    auto store = createTokenStore();
    const auto result = store->loadUserId();
    if (!result.hasValue()) {
        return {};
    }
    return result.value().trimmed();
}

QString LoginDialog::storedLicenseKey()
{
    QFile f(credPath(QStringLiteral("license_key.txt")));
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        return {};
    }
    return QTextStream(&f).readLine().trimmed();
}

void LoginDialog::storeCredentials(const QString& token, const QString& email, const QString& licenseKey)
{
    auto store = createTokenStore();
    if (!token.isEmpty()) {
        store->storeToken(token, email);
    }

    auto write = [](const QString& path, const QString& data) {
        QFile f(path);
        if (f.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
            QTextStream(&f) << data << "\n";
        }
    };
    if (!licenseKey.isEmpty())
    {
        write(credPath(QStringLiteral("license_key.txt")), licenseKey);
    }
}

void LoginDialog::clearCredentials()
{
    auto store = createTokenStore();
    store->clear();
    QFile::remove(credPath(QStringLiteral("license_key.txt")));
}

bool LoginDialog::hasStoredCredentials()
{
    return !storedToken().isEmpty() || !storedLicenseKey().isEmpty();
}

bool LoginDialog::signInWithBrowser(const QString& apiBaseUrl,
                                    QWidget* parent,
                                    const QString& preferredProvider,
                                    QString* outError)
{
    Q_UNUSED(parent);

    if (outError)
    {
        outError->clear();
    }

    const QString provider = preferredProvider.trimmed().isEmpty()
        ? QStringLiteral("google")
        : preferredProvider.trimmed().toLower();

    const auto oauthCfgResult = resolveSupabaseOAuthConfig(apiBaseUrl);
    if (!oauthCfgResult.hasValue())
    {
        if (outError) {
            *outError = oauthCfgResult.error().message;
        }
        return false;
    }
    const auto oauthCfg = oauthCfgResult.value();

    cppmonetize::OAuthConfig flowCfg;
    flowCfg.enabled = true;
    flowCfg.supabaseUrl = oauthCfg.supabaseUrl;
    flowCfg.supabaseAnonKey = oauthCfg.supabaseAnonKey;
    cppmonetize::OAuthDesktopFlow flow;
    const auto authResult = flow.signInWithBrowserPkce(flowCfg, provider, 180000);
    if (!authResult.hasValue())
    {
        if (outError) {
            *outError = authResult.error().message;
        }
        return false;
    }

    QString token = authResult.value().token.trimmed();
    QString email = authResult.value().email.trimmed();
    if (token.isEmpty())
    {
        if (outError) {
            *outError = QStringLiteral("OAuth callback did not return a token.");
        }
        return false;
    }
    if (email.isEmpty())
    {
        email = fetchEmailFromToken(apiBaseUrl, token);
    }

    QString licenseKey;
    cppmonetize::MonetizeClient client = createMonetizeClient(apiBaseUrl);
    const auto lic = client.getLicense(token);
    if (lic.hasValue()) {
        const QJsonObject resp = lic.value();
        if (!resp.isEmpty() && resp.value(QStringLiteral("active")).toBool()) {
            licenseKey = resp.value(QStringLiteral("license_key")).toString();
        }
    }

    storeCredentials(token, email, licenseKey);
    return true;
}

LoginDialog::LoginDialog(const QString& apiBaseUrl, QWidget* parent)
    : QDialog(parent)
    , apiBaseUrl_(apiBaseUrl)
{
    setWindowTitle(QStringLiteral("Motive Editor - Sign In"));
    setModal(true);
    setMinimumSize(360, 320);
    fitDialogToScreen(this, QSize(520, 560));
    setFont(QFont(QStringLiteral("Helvetica Neue"), 11));
    setStyleSheet(
        "QDialog { background-color: #1c1f26; color: #f4f7ff; }"
        "QLabel { color: #f4f7ff; }"
        "QFrame#card { background-color: #222734; border-radius: 16px; border: 1px solid #2c3446; }"
        "QPushButton#actionButton { background-color: #3d7cc9; color: white; padding: 10px 24px; border-radius: 8px; font-weight: bold; }"
        "QPushButton#actionButton:hover { background-color: #4b8add; }"
        "QPushButton#actionButton:disabled { background-color: #2a3040; color: #666; }"
        "QPushButton#switchButton { background-color: transparent; border: none; color: #8fc6ff; text-decoration: underline; }"
        "QPushButton#switchButton:hover { color: #b8daff; }"
        "QPushButton#skipButton { background-color: transparent; border: 1px solid #4b5470; color: #a0a8c0; padding: 8px 16px; border-radius: 8px; }"
        "QPushButton#skipButton:hover { border-color: #6b789b; color: #f4f7ff; }"
        "QLineEdit { background-color: #202635; color: #f4f7ff; border: 1px solid #323a4f; border-radius: 8px; padding: 9px 12px; }"
        "QLineEdit:focus { background-color: #242b3d; border-color: #5ea1ff; }"
        "QLabel#status { color: #ff6b6b; }"
        "QLabel#statusOk { color: #6bff8a; }");

    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(16);
    mainLayout->setContentsMargins(24, 24, 24, 24);

    titleLabel_ = new QLabel(QStringLiteral("<h2>Sign In to Motive Editor</h2>"), this);
    titleLabel_->setAlignment(Qt::AlignCenter);
    mainLayout->addWidget(titleLabel_);

    auto* card = new QFrame(this);
    card->setObjectName(QStringLiteral("card"));
    auto* cardLayout = new QVBoxLayout(card);
    cardLayout->setSpacing(14);
    cardLayout->setContentsMargins(20, 20, 20, 20);

    auto* emailLabel = new QLabel(QStringLiteral("Email"), this);
    emailLabel->setFont(QFont(QStringLiteral("Helvetica Neue"), 10, QFont::Bold));
    cardLayout->addWidget(emailLabel);
    emailEdit_ = new QLineEdit(this);
    emailEdit_->setPlaceholderText(QStringLiteral("you@example.com"));
    cardLayout->addWidget(emailEdit_);

    auto* pwLabel = new QLabel(QStringLiteral("Password"), this);
    pwLabel->setFont(QFont(QStringLiteral("Helvetica Neue"), 10, QFont::Bold));
    cardLayout->addWidget(pwLabel);
    passwordEdit_ = new QLineEdit(this);
    passwordEdit_->setEchoMode(QLineEdit::Password);
    passwordEdit_->setPlaceholderText(QStringLiteral("Enter password"));
    cardLayout->addWidget(passwordEdit_);

    confirmRow_ = new QWidget(this);
    auto* confirmLayout = new QVBoxLayout(confirmRow_);
    confirmLayout->setContentsMargins(0, 0, 0, 0);
    auto* confirmLabel = new QLabel(QStringLiteral("Confirm Password"), this);
    confirmLabel->setFont(QFont(QStringLiteral("Helvetica Neue"), 10, QFont::Bold));
    confirmLayout->addWidget(confirmLabel);
    passwordConfirmEdit_ = new QLineEdit(this);
    passwordConfirmEdit_->setEchoMode(QLineEdit::Password);
    passwordConfirmEdit_->setPlaceholderText(QStringLiteral("Confirm password"));
    confirmLayout->addWidget(passwordConfirmEdit_);
    confirmRow_->setVisible(false);
    cardLayout->addWidget(confirmRow_);

    statusLabel_ = new QLabel(QString(), this);
    statusLabel_->setObjectName(QStringLiteral("status"));
    statusLabel_->setWordWrap(true);
    statusLabel_->setAlignment(Qt::AlignCenter);
    cardLayout->addWidget(statusLabel_);

    actionButton_ = new QPushButton(QStringLiteral("Sign In"), this);
    actionButton_->setObjectName(QStringLiteral("actionButton"));
    cardLayout->addWidget(actionButton_);

    switchButton_ = new QPushButton(QStringLiteral("Don't have an account? Register"), this);
    switchButton_->setObjectName(QStringLiteral("switchButton"));
    switchButton_->setCursor(Qt::PointingHandCursor);
    cardLayout->addWidget(switchButton_, 0, Qt::AlignCenter);

    auto* orLabel = new QLabel(QStringLiteral("<center style='color: #6b789b;'>- or sign in with -</center>"), this);
    cardLayout->addWidget(orLabel);

    auto* oauthRow = new QHBoxLayout();
    oauthRow->setSpacing(12);
    auto makeOAuthButton = [this, oauthRow](const QString& provider, const QString& label, const QString& color) {
        auto* btn = new QPushButton(label, this);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setStyleSheet(
            QString("QPushButton { background-color: %1; color: white; padding: 10px 20px; "
                    "border-radius: 8px; font-weight: bold; border: none; }"
                    "QPushButton:hover { opacity: 0.9; }").arg(color));
        connect(btn, &QPushButton::clicked, this, [this, provider]() {
            startOAuthFlow(provider);
        });
        oauthRow->addWidget(btn);
    };
    makeOAuthButton(QStringLiteral("google"), QStringLiteral("Google"), QStringLiteral("#4285F4"));
    makeOAuthButton(QStringLiteral("github"), QStringLiteral("GitHub"), QStringLiteral("#333333"));
    cardLayout->addLayout(oauthRow);
    mainLayout->addWidget(card);

    skipButton_ = new QPushButton(QStringLiteral("Continue as Guest"), this);
    skipButton_->setObjectName(QStringLiteral("skipButton"));
    mainLayout->addWidget(skipButton_, 0, Qt::AlignCenter);

    connect(actionButton_, &QPushButton::clicked, this, [this]() {
        if (isRegisterMode_)
        {
            onRegisterClicked();
        }
        else
        {
            onLoginClicked();
        }
    });
    connect(switchButton_, &QPushButton::clicked, this, [this]() {
        if (isRegisterMode_)
        {
            switchToLogin();
        }
        else
        {
            switchToRegister();
        }
    });
    connect(skipButton_, &QPushButton::clicked, this, &LoginDialog::onSkipClicked);
    connect(passwordEdit_, &QLineEdit::returnPressed, actionButton_, &QPushButton::click);
    connect(passwordConfirmEdit_, &QLineEdit::returnPressed, actionButton_, &QPushButton::click);
    connect(emailEdit_, &QLineEdit::returnPressed, [this]() {
        passwordEdit_->setFocus();
    });
}

void LoginDialog::switchToRegister()
{
    isRegisterMode_ = true;
    titleLabel_->setText(QStringLiteral("<h2>Create Account</h2>"));
    actionButton_->setText(QStringLiteral("Register"));
    switchButton_->setText(QStringLiteral("Already have an account? Sign In"));
    confirmRow_->setVisible(true);
    statusLabel_->clear();
}

void LoginDialog::switchToLogin()
{
    isRegisterMode_ = false;
    titleLabel_->setText(QStringLiteral("<h2>Sign In to Motive Editor</h2>"));
    actionButton_->setText(QStringLiteral("Sign In"));
    switchButton_->setText(QStringLiteral("Don't have an account? Register"));
    confirmRow_->setVisible(false);
    statusLabel_->clear();
}

void LoginDialog::setStatus(const QString& msg, bool isError)
{
    statusLabel_->setObjectName(isError ? QStringLiteral("status") : QStringLiteral("statusOk"));
    statusLabel_->setStyleSheet(isError ? QStringLiteral("color: #ff6b6b;") : QStringLiteral("color: #6bff8a;"));
    statusLabel_->setText(msg);
}

void LoginDialog::setFieldsEnabled(bool enabled)
{
    emailEdit_->setEnabled(enabled);
    passwordEdit_->setEnabled(enabled);
    passwordConfirmEdit_->setEnabled(enabled);
    actionButton_->setEnabled(enabled);
    switchButton_->setEnabled(enabled);
    skipButton_->setEnabled(enabled);
}

void LoginDialog::onLoginClicked()
{
    doAuthRequest(QStringLiteral("/auth/login"));
}

void LoginDialog::onRegisterClicked()
{
    if (passwordEdit_->text() != passwordConfirmEdit_->text())
    {
        setStatus(QStringLiteral("Passwords do not match."), true);
        return;
    }
    if (passwordEdit_->text().length() < 6)
    {
        setStatus(QStringLiteral("Password must be at least 6 characters."), true);
        return;
    }
    doAuthRequest(QStringLiteral("/auth/register"));
}

void LoginDialog::doAuthRequest(const QString& endpoint)
{
    const QString em = emailEdit_->text().trimmed();
    const QString pw = passwordEdit_->text();
    const bool registerMode = (endpoint == QStringLiteral("/auth/register"));
    if (em.isEmpty() || pw.isEmpty())
    {
        setStatus(QStringLiteral("Email and password are required."), true);
        return;
    }

    setFieldsEnabled(false);
    setStatus(QStringLiteral("Connecting..."), false);

    cppmonetize::MonetizeClient client = createMonetizeClient(apiBaseUrl_);
    const auto browserCfgResult = resolveSupabaseOAuthConfig(apiBaseUrl_);
    bool usedDirectSupabase = false;
    if (browserCfgResult.hasValue()) {
        const auto browserCfg = browserCfgResult.value();
        usedDirectSupabase = true;
        cppmonetize::OAuthConfig oauthCfg;
        oauthCfg.enabled = true;
        oauthCfg.supabaseUrl = browserCfg.supabaseUrl;
        oauthCfg.supabaseAnonKey = browserCfg.supabaseAnonKey;
        cppmonetize::OAuthDesktopFlow flow;
        const auto directResult = flow.signInWithPassword(oauthCfg, em, pw, registerMode, 20000);
        if (!directResult.hasValue()) {
            setFieldsEnabled(true);
            setStatus(directResult.error().message.isEmpty()
                          ? QStringLiteral("Supabase sign-in failed.")
                          : directResult.error().message,
                      true);
            return;
        }
        token_ = directResult.value().token.trimmed();
        email_ = directResult.value().email.trimmed();
    }

    cppmonetize::Result<cppmonetize::AuthSession> authResult;
    if (!usedDirectSupabase) {
        authResult = registerMode ? client.registerUser(em, pw) : client.signIn(em, pw);
    }

    setFieldsEnabled(true);
    if (!usedDirectSupabase && !authResult.hasValue()) {
        const int code = authResult.error().statusCode;
        const QString detail = authResult.error().message;
        if (code == 401) {
            setStatus(QStringLiteral("Invalid email or password."), true);
        } else if (code == 409) {
            setStatus(QStringLiteral("Email already registered. Try signing in."), true);
        } else if (detail.isEmpty()) {
            setStatus(QStringLiteral("Connection failed. Check your internet."), true);
        } else {
            setStatus(detail, true);
        }
        return;
    }

    if (!usedDirectSupabase) {
        token_ = authResult.value().accessToken.trimmed();
        email_ = authResult.value().email.trimmed();
    }
    if (email_.isEmpty()) {
        const auto who = client.whoAmI(token_);
        if (who.hasValue()) {
            email_ = who.value().email.trimmed();
        }
    }

    const auto licenseResult = client.getLicense(token_);
    if (licenseResult.hasValue()) {
        const QJsonObject licResp = licenseResult.value();
        if (!licResp.isEmpty() && licResp.value(QStringLiteral("active")).toBool()) {
            licenseKey_ = licResp.value(QStringLiteral("license_key")).toString();
        }
    }

    storeCredentials(token_, email_, licenseKey_);
    setStatus(QStringLiteral("Signed in as %1").arg(email_), false);
    accept();
}

void LoginDialog::onSkipClicked()
{
    token_.clear();
    email_.clear();
    licenseKey_.clear();
    reject();
}

void LoginDialog::startOAuthFlow(const QString& provider)
{
    setStatus(QStringLiteral("Opening browser for %1 sign-in...").arg(provider), false);
    setFieldsEnabled(false);

    QString error;
    const bool ok = LoginDialog::signInWithBrowser(apiBaseUrl_, this, provider, &error);
    if (!ok)
    {
        setStatus(error.isEmpty() ? QStringLiteral("OAuth sign-in failed. Please try again.") : error, true);
        setFieldsEnabled(true);
        return;
    }

    token_ = storedToken();
    email_ = storedEmail();
    licenseKey_ = storedLicenseKey();
    setStatus(QStringLiteral("Signed in as %1").arg(email_.isEmpty() ? QStringLiteral("your account") : email_), false);
    accept();
}

QString LoginDialog::token() const
{
    return token_;
}

QString LoginDialog::email() const
{
    return email_;
}

QString LoginDialog::licenseKey() const
{
    return licenseKey_;
}

}  // namespace motive::ui
