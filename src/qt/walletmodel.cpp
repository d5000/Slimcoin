#include "walletmodel.h"
#include "guiconstants.h"
#include "optionsmodel.h"
#include "addresstablemodel.h"
#include "transactiontablemodel.h"
#include "inscriptiontablemodel.h"

#include "ui_interface.h"
#include "util.h"
#include "wallet.h"
#include "walletdb.h" // for BackupWallet
#include "bitcoinrpc.h" // getBurnCoinBalances()
#include "base58.h"
#include "smalldata.h"
#include "keystore.h"

#include <QSet>

WalletModel::WalletModel(CWallet *wallet, OptionsModel *optionsModel, QObject *parent) :
    QObject(parent), wallet(wallet), optionsModel(optionsModel), addressTableModel(0),
    transactionTableModel(0),
    cachedBalance(0), cachedReserveBalance(0), cachedUnconfirmedBalance(0), cachedNumTransactions(0),
    cachedEncryptionStatus(Unencrypted)
{
    addressTableModel = new AddressTableModel(wallet, this);
    transactionTableModel = new TransactionTableModel(wallet, this);
    inscriptionTableModel = new InscriptionTableModel(wallet, this);
}

qint64 WalletModel::getBalance() const
{
    return wallet->GetBalance();
}

qint64 WalletModel::getReserveBalance() const
{
    return wallet->GetReserveBalance();
}

qint64 WalletModel::getStake() const
{
    return wallet->GetStake();
}

qint64 WalletModel::getUnconfirmedBalance() const
{
    return wallet->GetUnconfirmedBalance();
}

qint64 WalletModel::getWatchBalance() const
{
    return wallet->GetWatchOnlyBalance();
}

qint64 WalletModel::getWatchUnconfirmedBalance() const
{
    return wallet->GetUnconfirmedWatchOnlyBalance();
}

qint64 WalletModel::getWatchImmatureBalance() const
{
    return wallet->GetImmatureWatchOnlyBalance();
}

BurnCoinsBalances WalletModel::getBurnCoinBalances() const
{
    int64 netBurnCoins, nEffBurnCoins, immatureCoins;
    ::getBurnCoinBalances(netBurnCoins, nEffBurnCoins, immatureCoins);

    return BurnCoinsBalances(netBurnCoins, nEffBurnCoins, immatureCoins);
}


int WalletModel::getNumTransactions() const
{
    int numTransactions = 0;
    {
        LOCK(wallet->cs_wallet);
        numTransactions = wallet->mapWallet.size();
    }
    return numTransactions;
}

void WalletModel::update()
{
    qint64 newBalance = getBalance();
    qint64 newReserveBalance = getReserveBalance();
    qint64 newUnconfirmedBalance = getUnconfirmedBalance();
    int newNumTransactions = getNumTransactions();
    EncryptionStatus newEncryptionStatus = getEncryptionStatus();

    BurnCoinsBalances newBurnBalances = getBurnCoinBalances();

    qint64 newWatchOnlyBalance = getWatchBalance();
    qint64 newWatchUnconfBalance = getWatchUnconfirmedBalance();
    qint64 newWatchImmatureBalance = getWatchImmatureBalance();

    if(cachedBalance != newBalance || cachedUnconfirmedBalance != newUnconfirmedBalance || cachedBurnCoinsBalances != newBurnBalances || cachedWatchOnlyBalance != newWatchOnlyBalance || cachedWatchUnconfBalance != newWatchUnconfBalance || cachedWatchImmatureBalance != newWatchImmatureBalance)
        emit balanceChanged(newBalance, newUnconfirmedBalance, newImmatureBalance,
                            newWatchOnlyBalance, newWatchUnconfBalance, newWatchImmatureBalance);
    if(cachedReserveBalance != newReserveBalance)
        emit reserveBalanceChanged(newReserveBalance);

    if(cachedNumTransactions != newNumTransactions)
        emit numTransactionsChanged(newNumTransactions);

    if(cachedEncryptionStatus != newEncryptionStatus)
        emit encryptionStatusChanged(newEncryptionStatus);

    cachedBalance = newBalance;
    cachedUnconfirmedBalance = newUnconfirmedBalance;
    cachedNumTransactions = newNumTransactions;
    cachedBurnCoinsBalances = newBurnBalances;
    cachedWatchOnlyBalance = newWatchOnlyBalance;
    cachedWatchUnconfBalance = newWatchUnconfBalance;
    cachedWatchImmatureBalance = newWatchImmatureBalance;
}

void WalletModel::updateAddressList()
{
    addressTableModel->update();
}

bool WalletModel::validateAddress(const QString &address)
{
    std::string sAddr = address.toStdString();
    CBitcoinAddress addressParsed(sAddr);
    return addressParsed.IsValid();
}

WalletModel::SendCoinsReturn WalletModel::sendCoins(const QList<SendCoinsRecipient> &recipients, QString &txmessage, bool fBurnTx)
{
    qint64 total = 0;
    QSet<QString> setAddress;
    QString hex;

    if(recipients.empty())
    {
        return OK;
    }

    // Pre-check input data for validity
    foreach(const SendCoinsRecipient &rcp, recipients)
    {
        if(!validateAddress(rcp.address))
        {
            return InvalidAddress;
        }
        setAddress.insert(rcp.address);

        if(rcp.amount < MIN_TXOUT_AMOUNT)
        {
            return InvalidAmount;
        }
        total += rcp.amount;
    }

    if(recipients.size() > setAddress.size())
    {
        return DuplicateAddress;
    }

    if(total > getBalance())
    {
        return AmountExceedsBalance;
    }

    if((total + nTransactionFee) > getBalance())
    {
        return SendCoinsReturn(AmountWithFeeExceedsBalance, nTransactionFee);
    }

    {
        LOCK2(cs_main, wallet->cs_wallet);

        // Sendmany
        std::vector<std::pair<CScript, int64> > vecSend;
        foreach(const SendCoinsRecipient &rcp, recipients)
        {
            CScript scriptPubKey;
            scriptPubKey.SetDestination(CBitcoinAddress(rcp.address.toStdString()).Get());
            vecSend.push_back(make_pair(scriptPubKey, rcp.amount));
        }

        if ( txmessage.length() )
        {
            std::string strmsg = txmessage.toStdString();
            const char *msg = strmsg.c_str();
            CScript scriptMsg;
            const unsigned char *msgHeader = GetSmallDataHeader(SMALLDATA_TYPE_PLAINTEXT);
            std::vector<unsigned char> vMsg;
            int i;
            for ( i = 0; i < 4; ++ i )
                vMsg.push_back(msgHeader[i]);
            for ( i = 0; i < std::strlen(msg); ++ i )
                vMsg.push_back(msg[i]);

            scriptMsg << OP_RETURN << vMsg;
            vecSend.push_back(make_pair(scriptMsg, CENT));
        }

        CWalletTx wtx;
        CReserveKey keyChange(wallet);
        int64 nFeeRequired = 0;
        bool fCreated = wallet->CreateTransaction(vecSend, wtx, keyChange, nFeeRequired);

        if(!fCreated)
        {
            if((total + nFeeRequired) > wallet->GetBalance())
            {
                return SendCoinsReturn(AmountWithFeeExceedsBalance, nFeeRequired);
            }
            return TransactionCreationFailed;
        }

        if(!ThreadSafeAskFee(nFeeRequired, tr("Sending...").toStdString()))
        {
            return Aborted;
        }
        //if they do not match, that means the user sent coins to a burn address from the send coins dialog
        if(wtx.IsBurnTx() != fBurnTx)
            return BadBurningCoins;

        //fBurnTx is to check if this transaction is supposed to be a burn transaction
        if(!wallet->CommitTransaction(wtx, keyChange, fBurnTx))
        {
            return TransactionCommitFailed;
        }
        hex = QString::fromStdString(wtx.GetHash().GetHex());
    }

    // Add addresses / update labels that we've sent to to the address book
    foreach(const SendCoinsRecipient &rcp, recipients)
    {
        std::string strAddress = rcp.address.toStdString();
        CTxDestination dest = CBitcoinAddress(strAddress).Get();
        std::string strLabel = rcp.label.toStdString();
        {
            LOCK(wallet->cs_wallet);

            std::map<CTxDestination, std::string>::iterator mi = wallet->mapAddressBook.find(dest);

            // Check if we have a new address or an updated label
            if ((mi == wallet->mapAddressBook.end() || mi->second != strLabel) && strLabel != "")
            {
                wallet->SetAddressBookName(dest, strLabel);
            }
        }
    }

    return SendCoinsReturn(OK, 0, hex);
}

OptionsModel *WalletModel::getOptionsModel()
{
    return optionsModel;
}

AddressTableModel *WalletModel::getAddressTableModel()
{
    return addressTableModel;
}

TransactionTableModel *WalletModel::getTransactionTableModel()
{
    return transactionTableModel;
}

InscriptionTableModel *WalletModel::getInscriptionTableModel()
{
    return inscriptionTableModel;
}

WalletModel::EncryptionStatus WalletModel::getEncryptionStatus() const
{
    if(!wallet->IsCrypted())
    {
        return Unencrypted;
    }
    else if(wallet->IsLocked())
    {
        return Locked;
    }
    else
    {
        return Unlocked;
    }
}

bool WalletModel::setWalletEncrypted(bool encrypted, const SecureString &passphrase)
{
    if(encrypted)
    {
        // Encrypt
        return wallet->EncryptWallet(passphrase);
    }
    else
    {
        // Decrypt -- TODO; not supported yet
        return false;
    }
}

bool WalletModel::setWalletLocked(bool locked, const SecureString &passPhrase)
{
    if(locked)
    {
        // Lock
        return wallet->LockKeyStore();
    }
    else
    {
        // Unlock
        return wallet->Unlock(passPhrase);
    }
}

bool WalletModel::changePassphrase(const SecureString &oldPass, const SecureString &newPass)
{
    bool retval;
    {
        LOCK(wallet->cs_wallet);
        wallet->LockKeyStore(); // Make sure wallet is locked before attempting pass change
        retval = wallet->ChangeWalletPassphrase(oldPass, newPass);
    }
    return retval;
}

bool WalletModel::backupWallet(const QString &filename)
{
    return BackupWallet(*wallet, filename.toLocal8Bit().data());
}

// WalletModel::UnlockContext implementation
WalletModel::UnlockContext WalletModel::requestUnlock()
{
    bool was_locked = getEncryptionStatus() == Locked;
    if ((!was_locked) && fWalletUnlockMintOnly)
    {
        setWalletLocked(true);
        was_locked = getEncryptionStatus() == Locked;
    }
    if(was_locked)
    {
        // Request UI to unlock wallet
        emit requireUnlock();
    }
    // If wallet is still locked, unlock was failed or cancelled, mark context as invalid
    bool valid = getEncryptionStatus() != Locked;

    return UnlockContext(this, valid, was_locked && !fWalletUnlockMintOnly);
}

WalletModel::UnlockContext::UnlockContext(WalletModel *wallet, bool valid, bool relock):
        wallet(wallet),
        valid(valid),
        relock(relock)
{
}

WalletModel::UnlockContext::~UnlockContext()
{
    if(valid && relock)
    {
        wallet->setWalletLocked(true);
    }
}

void WalletModel::UnlockContext::CopyFrom(const UnlockContext& rhs)
{
    // Transfer context; old object no longer relocks wallet
    *this = rhs;
    rhs.relock = false;
}

bool WalletModel::getPubKey(const CKeyID &address, CPubKey& vchPubKeyOut) const
{
     return wallet->GetPubKey(address, vchPubKeyOut);
}

CWallet * WalletModel::getWallet()
{
    return wallet;
}
