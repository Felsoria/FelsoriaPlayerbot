#ifndef FELSORIA_ARTIFACT_SHOP_POLICY_H
#define FELSORIA_ARTIFACT_SHOP_POLICY_H
#include <string>
namespace FelsoriaArtifactShop
{
    constexpr uint32 ProductID = 890201;
    constexpr uint32 GroupID = 8902;
    constexpr uint32 EntryID = 890201;
    constexpr uint64 Price = 25;
    constexpr uint64 FixedPrice = 250000;
    inline bool ConfirmationMatches(uint32 product, uint64 price, uint32 token,
        uint32 receivedToken, uint64 receivedPrice, bool confirmed)
    {
        return product == ProductID && price == Price && token && token == receivedToken &&
            receivedPrice == FixedPrice && confirmed;
    }
    inline std::string DebitSQL(uint32 account, uint64 purchase)
    {
        // One InnoDB statement changes both wallet and entitlement. A second
        // confirmation, another session or a retry cannot debit an owned row.
        return "UPDATE account a JOIN felsoria_artifact_skin_unlock u ON u.account_id=a.id "
            "SET a.balans=a.balans-25,u.owned=1,u.price_paid=25,u.purchase_id=" + std::to_string(purchase) +
            ",u.unlocked_at=UNIX_TIMESTAMP() WHERE a.id=" + std::to_string(account) +
            " AND u.owned=0 AND a.balans>=25";
    }
    inline bool OwnReceipt(bool owned, uint64 stored, uint64 requested)
    {
        return owned && requested && stored == requested;
    }
}
#endif
