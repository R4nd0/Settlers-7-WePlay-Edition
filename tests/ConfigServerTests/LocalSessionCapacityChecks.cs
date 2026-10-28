using System.Buffers.Binary;
using System.Text;
using S7Patcher.Source;

internal static class LocalSessionCapacityChecks
{
    internal static int Run()
    {
        int checks = 0;
        void Check(bool result, string label) { if (!result) throw new Exception(label); ++checks; }
        byte[] Number(string name, long value)
        {
            byte[] bytes = new byte[8]; BinaryPrimitives.WriteInt64LittleEndian(bytes, value);
            return [.. LocalSessionSearchChecks.Ddl(name), 6, .. bytes];
        }
        byte[] Slots(long free, long maximum = 4) => LocalSessionUpdateChecks.Map(
            Number("settlers_free_slots", free), Number("settlers_max_slots", maximum));
        string FreeKey = Convert.ToHexString("settlers_free_slots"u8);
        using var host = RegisteredHostChecks.CreateRegistered();
        var guests = Enumerable.Range(0, 4).Select(_ => RegisteredHostChecks.CreateRegistered()).ToArray();
        object owner = new(); var visitors = Enumerable.Range(0, 4).Select(_ => new object()).ToArray();
        try
        {
            using var registry = new LocalSessionRegistry(catalogueProbe: true);
            Check(registry.TryCreate(owner, host, LocalSessionCatalogueChecks.PublicBody(), out uint id, out _), "capacity host fixture");
            long ReadFree()
            {
                Check(registry.TryCopyLiveSession(new object(), "invented-space"u8, id, out var snapshot), "capacity snapshot");
                using (snapshot) return BinaryPrimitives.ReadInt64LittleEndian(snapshot.Metadata.Attributes[FreeKey].AsSpan(1));
            }
            bool Join(int index) => LocalSessionJoinCodec.TryReply(LocalSessionJoinChecks.Request(LocalSessionJoinChecks.Body(id:id)),
                registry, visitors[index], guests[index], out _, out _);
            bool Confirm(int index) => registry.TryConfirmJoin(visitors[index], guests[index], LocalJoinSuccessChecks.Body(id:id), out _);
            bool Update(long free) => registry.TryUpdate(owner, LocalSessionUpdateChecks.Body(id, attributes:Slots(free)), out _);
            for (int index = 0; index < 3; ++index)
            {
                Check(Join(index), "each of three guests can reserve a map slot");
                Check(Confirm(index) && Confirm(index), "confirmation/reconfirmation is idempotent");
                Check(Update(2 - index), "ordered host free-slot update after each confirmed guest");
                Check(ReadFree() == 2 - index, "confirmed human not subtracted twice from host report");
            }
            Check(!Join(3), "fourth guest rejected on four-player map");
            Check(Update(3) && ReadFree() == 0 && !Join(3), "even high host report cannot exceed absolute human/map cap");
            Check(registry.TryLeave(visitors[1], LocalSessionLeaveChecks.Body(id), out _), "confirmed guest leaves");
            Check(!registry.TryLeave(visitors[1], LocalSessionLeaveChecks.Body(id), out _), "duplicate leave cannot release twice");
            Check(Update(1) && ReadFree() == 1 && Join(3), "host follow-up frees exactly one slot for replacement guest");
            Check(ReadFree() == 0, "unconfirmed replacement reserves one slot");
            Check(!registry.TryLeave(new object(), LocalSessionLeaveChecks.Body(id), out _) && ReadFree() == 0,
                "foreign leave cannot release reservation");
            registry.RemoveOwner(visitors[3]); registry.RemoveOwner(visitors[3]);
            Check(ReadFree() == 1 && Join(3), "transport cleanup is idempotent and permits rejoin");
            Check(Confirm(3), "replacement can complete normally");
            Check(registry.TryLeave(owner, LocalSessionLeaveChecks.Body(id), out _) && registry.Count == 0,
                "host leave clears only its complete lobby");
            Check(registry.TryCreate(owner, host, LocalSessionCatalogueChecks.PublicBody(), out uint rehost, out _) && rehost != id,
                "same connection can rehost after return to browser");
            Check(!registry.TryConfirmJoin(visitors[0], guests[0], LocalJoinSuccessChecks.Body(id:id), out _), "old confirmation cannot enter replacement lobby");

            // An authoritative later host update may free an AI/closed slot.
            using var ai = new LocalSessionRegistry(catalogueProbe: true);
            Check(ai.TryCreate(owner, host, LocalSessionCatalogueChecks.PublicBody(), out uint aiId, out _)
                && ai.TryUpdate(owner, LocalSessionUpdateChecks.Body(aiId, attributes:Slots(1)), out _), "two AI slots fixture");
            Check(ai.TryJoin(visitors[0], guests[0], LocalSessionJoinChecks.Body(id:aiId), _ => [1], out _, out _)
                && ai.TryConfirmJoin(visitors[0], guests[0], LocalJoinSuccessChecks.Body(id:aiId), out _), "one available human joins");
            Check(ai.TryUpdate(owner, LocalSessionUpdateChecks.Body(aiId, attributes:Slots(0)), out _), "host reports full human/AI map");
            Check(!ai.TryJoin(visitors[1], guests[1], LocalSessionJoinChecks.Body(id:aiId), _ => [1], out _, out _), "AI occupied slot cannot be reserved");
            Check(ai.TryUpdate(owner, LocalSessionUpdateChecks.Body(aiId, attributes:Slots(1)), out _)
                && ai.TryJoin(visitors[1], guests[1], LocalSessionJoinChecks.Body(id:aiId), _ => [1], out _, out _),
                "ordered AI removal allows another human without rehosting");
        }
        finally { foreach (var guest in guests) guest.Dispose(); }
        return checks;
    }
}
