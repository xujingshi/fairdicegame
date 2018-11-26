#include <algorithm>
#include <eosiolib/transaction.hpp>
#include <eosiolib/symbol.hpp>
#include "eosio.token.hpp"
#include "types.hpp"

using namespace eosio;

class [[eosio::contract]] fairdicegame : public contract {

public:
  struct [[eosio::table]] st_bet {
      uint64_t id;
      account_name player;
      account_name referrer;
      //asset amount;
      int64_t amount;
      eosio::symbol_type symbol;
      uint8_t roll_under;
      checksum256 seed_hash;
      checksum160 user_seed_hash;
      uint64_t created_at;
      uint64_t primary_key() const { return id; }
  };
  
  struct st_result {
      uint64_t bet_id;
      account_name player;
      account_name referrer;
      asset amount;
      uint8_t roll_under;
      uint8_t random_roll;
      checksum256 seed;
      checksum256 seed_hash;
      checksum160 user_seed_hash;
      asset payout;
  };
  
  struct [[eosio::table]] st_hash {
      checksum256 hash;
      uint64_t expiration;
      uint64_t primary_key() const { return uint64_hash(hash); }
  
      uint64_t by_expiration() const { return expiration; }
  };
  
  struct [[eosio::table]] st_fund_pool {
      uint64_t id;
      int64_t locked;
      //asset locked;
  
      uint64_t primary_key() const { return id; }
  };
  
  struct [[eosio::table]] st_global {
      uint64_t current_id;
  };
  typedef eosio::multi_index<N(bets), st_bet> tb_bets;
  typedef eosio::singleton<N(fundpool), st_fund_pool> tb_fund_pool;
  typedef eosio::singleton<N(global), st_global> tb_global;
  
  typedef eosio::multi_index<N(fundpool), st_fund_pool> dump_for_st_fund_pool;
  typedef eosio::multi_index<N(global), st_global> dump_for_st_global;
  
  typedef eosio::multi_index<
      N(hash),
      st_hash,
      indexed_by<N(by_expiration),
      const_mem_fun<st_hash, uint64_t, &st_hash::by_expiration>>> tb_hash;

   public:
    fairdicegame(account_name self) : eosio::contract(self),
          _bets(_self, _self),
          _fund_pool(_self, _self),
          _hash(_self, _self),
          _global(_self, _self)
    {};

    [[eosio::action]]
    void transfer(const account_name& from, const account_name& to);

    [[eosio::action]]
    void receipt(const st_bet& bet);

    [[eosio::action]]
    void reveal(const uint64_t& id, const checksum256& seed);

   private:
    tb_bets _bets;
    tb_fund_pool _fund_pool;
    tb_hash _hash;
    tb_global _global;

    void parse_memo(string memo,
                    uint8_t* roll_under,
                    checksum256* seed_hash,
                    checksum160* user_seed_hash,
                    uint64_t* expiration,
                    account_name* referrer,
                    signature* sig) {
        // remove space
        memo.erase(std::remove_if(memo.begin(),
                                  memo.end(),
                                  [](unsigned char x) { return std::isspace(x); }),
                   memo.end());

        size_t sep_count = std::count(memo.begin(), memo.end(), '-');
        eosio_assert(sep_count == 5, "invalid memo");

        size_t pos;
        string container;
        pos = sub2sep(memo, &container, '-', 0, true);
        eosio_assert(!container.empty(), "no roll under");
        *roll_under = stoi(container);
        pos = sub2sep(memo, &container, '-', ++pos, true);
        eosio_assert(!container.empty(), "no seed hash");
        *seed_hash = hex_to_sha256(container);
        pos = sub2sep(memo, &container, '-', ++pos, true);
        eosio_assert(!container.empty(), "no user seed hash");
        *user_seed_hash = hex_to_sha1(container);
        pos = sub2sep(memo, &container, '-', ++pos, true);
        eosio_assert(!container.empty(), "no expiration");
        *expiration = stoull(container);
        pos = sub2sep(memo, &container, '-', ++pos, true);
        eosio_assert(!container.empty(), "no referrer");
        *referrer = string_to_name(container.c_str());
        container = memo.substr(++pos);
        eosio_assert(!container.empty(), "no signature");
        *sig = str_to_sig(container);
    }

    uint8_t compute_random_roll(const checksum256& seed1, const checksum160& seed2) {
        size_t hash = 0;
        hash_combine(hash, sha256_to_hex(seed1));
        hash_combine(hash, sha1_to_hex(seed2));
        return hash % 100 + 1;
    }

    asset compute_referrer_reward(const st_bet& bet) { 
      return asset(bet.amount / 200, bet.symbol);
    }

    uint64_t next_id() {
        st_global global = _global.get_or_default(
            st_global{.current_id = _bets.available_primary_key()});
        global.current_id += 1;
        _global.set(global, _self);
        return global.current_id;
    }

    string referrer_memo(const st_bet& bet) {
        string memo = "bet id:";
        string id = uint64_string(bet.id);
        memo.append(id);
        memo.append(" player: ");
        string player = name{bet.player}.to_string();
        memo.append(player);
        memo.append(" referral reward! - dapp.pub/dice/");
        return memo;
    }

    string winner_memo(const st_bet& bet) {
        string memo = "bet id:";
        string id = uint64_string(bet.id);
        memo.append(id);
        memo.append(" player: ");
        string player = name{bet.player}.to_string();
        memo.append(player);
        memo.append(" winner! - dapp.pub/dice/");
        return memo;
    }

    st_bet find_or_error(const uint64_t& id) {
        auto itr = _bets.find(id);
        eosio_assert(itr != _bets.end(), "bet not found");
        return *itr;
    }

    void assert_hash(const checksum256& seed_hash, const uint64_t& expiration) {
        const uint32_t _now = now();

        // check expiratin
        eosio_assert(expiration > _now, "seed hash expired");

        // check hash duplicate
        const uint64_t key = uint64_hash(seed_hash);
        auto itr = _hash.find(key);
        eosio_assert(itr == _hash.end(), "hash duplicate");

        // clean up
        auto index = _hash.get_index<N(by_expiration)>();
        auto upper_itr = index.upper_bound(_now);
        auto begin_itr = index.begin();
        auto count = 0;
        while ((begin_itr != upper_itr) && (count < 3)) {
            begin_itr = index.erase(begin_itr);
            count++;
        }

        // save hash
        _hash.emplace(_self, [&](st_hash& r) {
            r.hash = seed_hash;
            r.expiration = expiration;
        });
    }

    void assert_quantity(const asset& quantity) {
        eosio_assert(quantity.symbol == EOS_SYMBOL, "only EOS token allowed");
        eosio_assert(quantity.is_valid(), "quantity invalid");
        eosio_assert(quantity.amount >= 1000, "transfer quantity must be greater than 0.1");
    }

    void assert_roll_under(const uint8_t& roll_under, const asset& quantity) {
        eosio_assert(roll_under >= 2 && roll_under <= 96,
                     "roll under overflow, must be greater than 2 and less than 96");
        auto payout = max_payout(roll_under, quantity);
        max_bonus();
        // eosio_assert(
        //    payout <= max_bonus(),
        //    "offered overflow, expected earning is greater than the maximum bonus");
    }

    void save(const st_bet& bet) {
        _bets.emplace(_self, [&](st_bet& r) {
            r.id = bet.id;
            r.player = bet.player;
            r.referrer = bet.referrer;
            r.amount = bet.amount;
            r.roll_under = bet.roll_under;
            r.seed_hash = bet.seed_hash;
            r.user_seed_hash = bet.user_seed_hash;
            r.created_at = bet.created_at;
        });
    }

    void remove(const st_bet& bet) { _bets.erase(bet); }

    void unlock(const asset& amount) {
        st_fund_pool pool = get_fund_pool();
        pool.locked -= amount.amount;
        eosio_assert(pool.locked >= 0, "fund unlock error");
        _fund_pool.set(pool, _self);
    }

    void lock(const asset& amount) {
        st_fund_pool pool = get_fund_pool();
        pool.locked += amount.amount;
        _fund_pool.set(pool, _self);
    }

    asset compute_payout(const uint8_t& roll_under, const asset& offer) {
        return min(max_payout(roll_under, offer), max_bonus());
    }
    asset max_payout(const uint8_t& roll_under, const asset& offer) {
        const double ODDS = 98.0 / ((double)roll_under - 1.0);
        return asset(ODDS * offer.amount, offer.symbol);
    }

    asset max_bonus() { return available_balance() / 100; }

    asset available_balance() {
        auto token = eosio::token(N(eosio.token));
        const asset balance = asset(1000000, symbol_type(EOS_SYMBOL));

        token.get_balance(_self, balance.symbol.code().raw());

        // const uint64_t locked = get_fund_pool().locked;
        // const asset available = asset(balance.amount - locked, symbol_type(EOS_SYMBOL).name());
        // eosio_assert(available.amount >= 0, "fund pool overdraw");
        // return available;
        return balance;
    }

    st_fund_pool get_fund_pool() {
        st_fund_pool fund_pool{.locked = 0, .id = 0};
        return _fund_pool.get_or_create(_self, fund_pool);
    }

    void assert_signature(const uint8_t& roll_under,
                          const checksum256& seed_hash,
                          const uint64_t& expiration,
                          const account_name& referrer,
                          const signature& sig) {
        string data = uint64_string(roll_under);
        data += "-";
        data += sha256_to_hex(seed_hash);
        data += "-";
        data += uint64_string(expiration);
        data += "-";
        data += name{referrer}.to_string();

        checksum256 digest;
        const char* data_cstr = data.c_str();
        sha256(data_cstr, strlen(data_cstr), &digest);
        public_key key = str_to_pub(PUB_KEY, false);
        assert_recover_key(&digest,
                           (char*)&sig.data,
                           sizeof(sig.data),
                           (char*)&key.data,
                           sizeof(key.data));
    }

    void assert_seed(const checksum256& seed, const checksum256& hash) {
        string seed_str = sha256_to_hex(seed);
        assert_sha256(seed_str.c_str(),
                      strlen(seed_str.c_str()),
                      (const checksum256*)&hash);
    }

    template <typename... Args>
    void send_defer_action(Args&&... args) {
        transaction trx;
        trx.actions.emplace_back(std::forward<Args>(args)...);
        trx.send(next_id(), _self, false);
    }
};

#define EOSIO_ABI_EX( TYPE, MEMBERS ) \
extern "C" { \
   void apply( uint64_t receiver, uint64_t code, uint64_t action ) { \
      auto self = receiver; \
      if( code == self || code == N(eosio.token)) { \
      	 if( action == N(transfer)){ \
      	 	eosio_assert( code == N(eosio.token), "Must transfer EOS"); \
      	 } \
         TYPE thiscontract( self ); \
         switch( action ) { \
            EOSIO_API( TYPE, MEMBERS ) \
         } \
         /* does not allow destructor of thiscontract to run: eosio_exit(0); */ \
      } \
   } \
}

EOSIO_ABI_EX( fairdicegame, 
	(receipt)
	(reveal)
	(transfer)
)
//
//
//extern "C" {
//void apply(uint64_t receiver, uint64_t code, uint64_t action) {
//    fairdicegame thiscontract(receiver);
//
//    if ((code == N(eosio.token)) && (action == N(transfer))) {
//        execute_action(&thiscontract, &fairdicegame::transfer);
//        return;
//    }
//
//    if (code != receiver) return;
//
//    switch (action) { EOSIO_API(fairdicegame, (receipt)(reveal)(transfer)) };
//    eosio_exit(0);
//}
//}
