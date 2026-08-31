// Port of account src/account.ts. The Data template parameter mirrors the
// upstream AccountObject generic (chain-specific data types like
// TelosAccountObject/WAXAccountObject).
#pragma once

#include <dwarfkit/account/permission.hpp>
#include <dwarfkit/account/resource.hpp>
#include <dwarfkit/account/system_contract.hpp>
#include <dwarfkit/resources.hpp>
#include <dwarfkit/token.hpp>

namespace dwarfkit {

struct BuyramOptions {
    std::optional<Name> receiver;
};

struct DelegateOptions {
    std::optional<Name> from;
    std::optional<Name> receiver;
    std::optional<Asset> cpu;
    std::optional<Asset> net;
    std::optional<bool> transfer;
};

struct UndelegateOptions {
    std::optional<Name> from;
    std::optional<Name> receiver;
    std::optional<Asset> cpu;
    std::optional<Asset> net;
};

template <typename Data = api::v1::AccountObject>
class Account {
public:
    struct Args {
        std::shared_ptr<APIClient> client;
        std::optional<Contract> contract;
        Data data;
    };

    explicit Account(Args args)
        : data(std::move(args.data)),
          systemContract(args.contract ? *args.contract
                                       : system_contract::contract(args.client)),
          client(args.client),
          token({.client = args.client}) {}

    Data data;
    Contract systemContract;
    std::shared_ptr<APIClient> client;
    Token token;

    Name accountName() const { return data.account_name; }

    // The system token symbol, read from the account's staked resources.
    Result<Asset::Symbol> systemToken() const {
        if (!data.total_resources) {
            return err(ErrorKind::Invalid, "Account has no total_resources data.");
        }
        return data.total_resources->cpu_weight.symbol;
    }

    Result<Asset> balance(const std::optional<std::string>& symbol = {},
                          const std::optional<Name>& tokenContract = {}) const {
        return token.balance(accountName(), symbol, tokenContract);
    }

    Result<Permission> permission(const Name& permissionName) const {
        const auto it = std::find_if(
            data.permissions.begin(), data.permissions.end(),
            [&](const api::v1::AccountPermission& p) { return p.perm_name == permissionName; });
        if (it == data.permissions.end()) {
            return err(ErrorKind::NotFound, "Permission " + permissionName.toString() +
                                                " does not exist on account " +
                                                accountName().toString() + ".");
        }
        return Permission::from(*it);
    }

    std::vector<Permission> permissions() const {
        std::vector<Permission> rv;
        rv.reserve(data.permissions.size());
        for (const auto& p : data.permissions) {
            rv.push_back(Permission::from(p));
        }
        return rv;
    }

    Resource resource(ResourceType resourceType) const { return Resource(resourceType, data); }

    // An instance of the resources library configured for this
    // blockchain/account.
    Result<Resources> resources(const std::optional<Name>& sampleAccount = {}) const {
        ResourcesOptions options;
        options.api = client;
        if (sampleAccount) {
            options.sampleAccount = sampleAccount->toString();
        }
        if (data.core_liquid_balance) {
            options.symbol = data.core_liquid_balance->symbol.toString();
        }
        return Resources::make(options);
    }

    Result<Action> setPermission(const Permission& permission) const {
        return systemContract.action(
            Name::from("updateauth"),
            json{{"account", accountName().toString()},
                 {"auth", Serializer::objectify(permission.required_auth)},
                 {"authorized_by", ""},
                 {"parent", permission.parent.toString()},
                 {"permission", permission.perm_name.toString()}});
    }

    Result<Action> removePermission(const Name& permissionName) const {
        return systemContract.action(Name::from("deleteauth"),
                                     json{{"account", accountName().toString()},
                                          {"authorized_by", ""},
                                          {"permission", permissionName.toString()}});
    }

    Result<Action> linkauth(const Name& contract, const Name& action,
                            const Name& requiredPermission) const {
        return systemContract.action(Name::from("linkauth"),
                                     json{{"account", accountName().toString()},
                                          {"code", contract.toString()},
                                          {"type", action.toString()},
                                          {"requirement", requiredPermission.toString()},
                                          {"authorized_by", ""}});
    }

    Result<Action> unlinkauth(const Name& contract, const Name& action) const {
        return systemContract.action(Name::from("unlinkauth"),
                                     json{{"account", accountName().toString()},
                                          {"code", contract.toString()},
                                          {"type", action.toString()},
                                          {"authorized_by", ""}});
    }

    Result<Action> buyRam(const Asset& amount, const BuyramOptions& options = {}) const {
        const Name receiver = options.receiver.value_or(accountName());
        return systemContract.action(Name::from("buyram"),
                                     json{{"payer", accountName().toString()},
                                          {"quant", amount.toString()},
                                          {"receiver", receiver.toString()}});
    }

    Result<Action> buyRamBytes(uint32_t bytes, const BuyramOptions& options = {}) const {
        const Name receiver = options.receiver.value_or(accountName());
        return systemContract.action(Name::from("buyrambytes"),
                                     json{{"bytes", bytes},
                                          {"payer", accountName().toString()},
                                          {"receiver", receiver.toString()}});
    }

    Result<Action> sellRam(uint32_t bytes) const {
        return systemContract.action(Name::from("sellram"),
                                     json{{"account", accountName().toString()},
                                          {"bytes", bytes}});
    }

    Result<Action> delegate(const DelegateOptions& value) const {
        DK_TRY(symbol, systemToken());
        const Asset zero = Asset::fromUnits(0, symbol);
        return systemContract.action(
            Name::from("delegatebw"),
            json{{"from", value.from.value_or(accountName()).toString()},
                 {"receiver", value.receiver.value_or(accountName()).toString()},
                 {"stake_cpu_quantity", value.cpu.value_or(zero).toString()},
                 {"stake_net_quantity", value.net.value_or(zero).toString()},
                 {"transfer", value.transfer.value_or(false)}});
    }

    Result<Action> undelegate(const UndelegateOptions& value) const {
        DK_TRY(symbol, systemToken());
        const Asset zero = Asset::fromUnits(0, symbol);
        return systemContract.action(
            Name::from("undelegatebw"),
            json{{"from", value.from.value_or(accountName()).toString()},
                 {"receiver", value.receiver.value_or(accountName()).toString()},
                 {"unstake_cpu_quantity", value.cpu.value_or(zero).toString()},
                 {"unstake_net_quantity", value.net.value_or(zero).toString()}});
    }
};

}  // namespace dwarfkit
