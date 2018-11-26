#include "fairdicegame.hpp"

using namespace eosio;

void fairdicegame::reveal(const uint64_t& id, const checksum256& seed) {
    require_auth(REVEALER);
    st_bet bet = find_or_error(id);
    assert_seed(seed, bet.seed_hash);

    uint8_t random_roll = compute_random_roll(seed, bet.user_seed_hash);
    asset payout = asset(0, EOS_SYMBOL);
    if (random_roll < bet.roll_under) {
        payout = compute_payout(bet.roll_under, asset(bet.amount, bet.symbol));
        action(permission_level{_self, N(active)},
               N(eosio.token),
               N(transfer),
               make_tuple(_self, bet.player, payout, winner_memo(bet)))
            .send();
    }
    unlock(asset(bet.amount, bet.symbol));
    if (bet.referrer != _self) {
        // defer trx, no need to rely heavily
        send_defer_action(permission_level{_self, N(active)},
                          N(eosio.token),
                          N(transfer),
                          make_tuple(_self,
                                     bet.referrer,
                                     compute_referrer_reward(bet),
                                     referrer_memo(bet)));
    }
    remove(bet);
    st_result result{.bet_id = bet.id,
                     .player = bet.player,
                     .referrer = bet.referrer,
                     .amount = asset(bet.amount, bet.symbol),
                     .roll_under = bet.roll_under,
                     .random_roll = random_roll,
                     .seed = seed,
                     .seed_hash = bet.seed_hash,
                     .user_seed_hash = bet.user_seed_hash,
                     .payout = payout};

    send_defer_action(permission_level{_self, N(active)},
                      LOG,
                      N(result),
                      result);
}

void fairdicegame::transfer(const account_name& sender, const account_name& receiver) {
    constexpr size_t max_stack_buffer_size = 512;
    size_t size = action_data_size();
    const bool heap_allocation = max_stack_buffer_size < size;
    char* buffer = (char*)( heap_allocation ? malloc(size) : alloca(size) );
    read_action_data( buffer, size );

    asset quantity = asset(uint64_t(100), symbol_type(EOS_SYMBOL)); 
    std::string memo;
    account_name from, to;
    auto msg = std::make_tuple(from, to, quantity, memo);

    datastream<const char*> ds(buffer,size);
    ds >> msg;

    from = std::get<0>(msg);
    to = std::get<1>(msg);
    quantity = std::get<2>(msg);
    memo = std::get<3>(msg);
    //print_f("transfer_from: %, transfer_to: %, memo: %\n", from, to, memo);
    if (from == _self || to != _self) {
        return;
    }
    if ("Transfer bonus" == memo) {
        return;
    }

    uint8_t roll_under;
    checksum256 seed_hash;
    checksum160 user_seed_hash;
    uint64_t expiration;
    account_name referrer;
    signature sig;

    parse_memo(memo, &roll_under, &seed_hash, &user_seed_hash, &expiration, &referrer, &sig);

    //check quantity
    assert_quantity(quantity);

    //check roll_under
    assert_roll_under(roll_under, quantity);

    //check seed hash && expiration
    //assert_hash(seed_hash, expiration);

    //check referrer
    eosio_assert(referrer != from, "referrer can not be self");

    //check signature
    //assert_signature(roll_under, seed_hash, expiration, referrer, sig);

    const st_bet _bet{.id = next_id(),
                      .player = from,
                      .referrer = referrer,
                      .amount = quantity.amount,
                      .symbol = quantity.symbol,
                      .roll_under = roll_under,
                      .seed_hash = seed_hash,
                      .user_seed_hash = user_seed_hash,
                      .created_at = now()};
    save(_bet);
    lock(quantity);
    action(permission_level{_self, N(active)},
           _self,
           N(receipt),
           _bet)
        .send();
}

void fairdicegame::receipt(const st_bet& bet) {
    require_auth(_self);
}
