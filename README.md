# New Horizons

## Before you begin

Heroes III was love at first sight for me, back in the early 2000s. After Heroes III, I played Heroes IV, Then Heroes II, then Heroes I, then Heroes V, then King’s Bounty: The Legend, then Heroes VII. Each installment has its merits, in my opinion, but none hooked me quite like the third one. I kept coming back. And over the years, I collected ideas.

Some came from other Heroes games. Some came from noticing a choice I almost never made, a skill I dreaded being offered, or a hero whose name promised a more distinctive experience than I felt the rules delivered. Over the years, that collection brewed a storm in my mind.

My first attempts to make these ideas actually playable were in VCMI. I began by trying to put things into the game, then found myself reconsidering more and more of what was already there. Changing magic led me back to skills. Changing skills led me to hero classes.

### A debt of gratitude

The HotA team has my enormous respect. Their stated ambition is to make an expansion that could belong alongside New World Computing’s own, with care extending through the gameplay, graphics, music, and even the details a player might barely notice. That is a demanding standard, and I admire the seriousness with which they pursue it.

VCMI deserves a particularly heartfelt thank-you. Its contributors have undertaken the work of rebuilding Heroes III’s engine from scratch and sharing it as an open-source project, maintained by volunteers. Truly impressive and beautiful work.

This project is a fork of VCMI, but I decided to detach it after parting ways with the project; this isn’t a recreation of VCMI with new mod support or anything like that. No, not at all. This is the version of Heroes III I’ve long dreamed of bringing to life. VCMI is the foundation, the engine, the mountain atop which I’m building the castle whose doors I’m opening to you now.

My eventual change of direction carries no diminished regard for their work. To everyone who has contributed to VCMI: thank you. I know enough about the task to appreciate how much I owe that starting point.

## What’s the difference between a Knight and a Wizard?

Take a moment to answer that.

Heroes III gives us an answer through its attributes and advancement rules. Knights tend to develop Attack and Defense; Wizards tend toward Power and Knowledge. Every hero develops the same 4 attributes, and the original manual explains that advancement generally becomes more balanced beyond level 9. For my taste, I’ve always found the classes to be too homogeneous, even across the divide between Might and Magic.

Moreover, in the original game, a Knight’s martial attributes work automatically: Attack and Defense are added to the corresponding statistics of the creatures under his command. His military expertise is already helping before I’ve decided how to use it.

In this series, with the notable exception of Heroes IV, heroes are generals, and generals ought to have martial abilities worth actively using in battle!

### Orders

That’s where Orders come in. In New Horizons, a hero normally has 1 Hero Action each combat round, spent either casting a Spell or issuing an Order. An Order costs no Mana; its cost is the opportunity to use that action for something else.

Charge! rewards troops that advance far enough before making a melee attack. Focus Fire! directs your shooters against a chosen enemy and eases the range and obstacle penalties obstructing their shots. Hold the Line! protects stacks that remain in position, while Riposte! prepares your army to withstand melee attacks and answer them harder, and so on…

And every ordinary Order competes with your spellcasting for that round. Sometimes saving Mana is sensible. Sometimes the spell is worth far more than the command you’d otherwise issue. That does not mean Might Heroes only use orders while Magic Heroes only use spells. No, the Knight keeps his spellbook, and the Wizard can give orders (I wrote this, and immediately thought to myself ‘this reads too much like AI’, but I liked the contrast, and the imagery it provoked in me, so I kept it).

Their specializations make the choice different. Might heroes can develop Command, which increases the contribution their attributes make to Orders. Magic heroes can develop Wisdom, which reduces ordinary spell costs and supports their magical endurance.

### Primary skills -> Attributes

I’ve also changed how the attributes themselves work. Hero Attack and Defense are no longer automatically added to creature statistics. Their strength is expressed through Orders and other mechanics that explicitly use them.

Class growth is predictable. A Knight develops Defense most strongly, followed by Attack. A Wizard develops Spell Power and Knowledge equally, with very little natural martial growth. Skills provide room for individual development while the class continues to have a recognizable direction.

There’s room for heroes who work across the divide, too. Warcasting rewards alternating between Spells and Orders: a spell prepares a stronger subsequent order, and an order prepares a stronger subsequent spell. I wanted the “battle mage” classes to feel like actual hybrid classes, not merely half as good in might, and in magic. No, no, I wanted that double-proficiency to actually emerge from simple rules. To a certain extent, I believe that goal was fulfilled.

### And what’s the difference between 2 Knights?

That question matters just as much.

Skills retain the familiar Basic, Advanced, and Expert ranks. Each also has a pool of 10 perks, of which a hero can learn at most 3. Under normal advancement, ranks and perks alternate, so developing a discipline involves choosing how to specialize along the way.

An offensive commander might take Shock Assault, letting attacks under Charge! ignore part of the enemy’s Defense. Another might choose Encirclement to make attacks from additional sides more rewarding. Those choices give the same Orders different tactical weight.

The perk system also gives some familiar skills a more useful home. Navigation, Pathfinding, and Scouting sit within Logistics. Scholar and Eagle Eye belong to Learning. Artillery, Ballistics, and First Aid come together under War Machines, with perks for the particular kind of military engineering you want to pursue.

### The town you come from should also matter

Each faction has its own unique Skill, available to both of its hero classes. Gone are the days of this belonging exclusively to Necropolis.

These unique skills use the same rank and perk structure as the other Skills, so faction identity also leaves room for personal choices.

Magic follows the same concern for identity. There are now 6 schools: Light, Shadow, Nature, Havoc, Sorcery, and Chaos. Each town favors a distinct pair, and my idea was to provide spells that actually make sense according to the, say, philosophy of that town. It always bothered me, in the original game, that fire magic had the weakest damage-dealing spells, for instance.

## The trouble with 137 angels

A great deal of mystery is dispelled once you have 137 angels instead of 2.

Playing Baldur’s Gate 3 helped me put a finger on that feeling. Small numbers can carry an extraordinary amount of weight when you care about what they describe. I wanted to bring some of that closeness into Heroes. Its armies give me a different kind of attachment, spread across the creatures I recruit and the formations I learn to rely on.

An angel should remain an extraordinary thing to have at your side.

That’s part of the reason for Leadership. In New Horizons, Leadership is a Secondary Attribute that limits how many creatures a hero can command in each stack (yes, exactly like in really great, and often forgotten, King’s Bounty: The Legend).

Every creature has a Leadership Requirement, and each of the army’s 7 slots is checked independently against the hero’s capacity. Filling one slot doesn’t reduce the capacity available in the others. That allows the same commander to lead a substantial group of ordinary troops alongside a small number of exceptional creatures. Leadership develops with the hero, and its progression favors naturally martial classes. Leading larger formations becomes another part of their identity.

The early battles in Heroes already give me reasons to count carefully. I’d like to carry that attention further into the game, through armies whose size leaves room for their character.

## Why change a game I love this much?

Because I’ve loved it long enough to keep imagining possibilities.

I wanted to bring together the ideas from the Heroes series that stayed with me, then work out how they could belong in this particular reimagining. Sometimes that meant adding something. Sometimes it meant taking a familiar rule apart and discovering how many other rules were leaning on it.

My aim is the Heroes III I’ve wanted to play for years: heroes whose development gives me new plans to try, and towns whose character runs through their mechanics. I want familiar battles to make me stop and think again.

Some changes are substantial. You’ll probably disagree with a few of them. I expect that, especially from someone who brings their own long history with the game.

## A note on AI, with the human still attached

I used a great deal of generative AI to make this mod. It helped me write code, and much of the new artwork was either generated with it, or AI helped me polish it. You deserve to know that.

I’m a computer engineer with a very old affection for Heroes III and a list of changes that became increasingly inconsiderate of my available time. These tools helped me turn more of that list into something you can actually play.

### A much older idea

In March 1960, J. C. R. Licklider published Man-Computer Symbiosis. His proposed division of labor included a sentence I’d happily keep above my desk: “men will set the goals, formulate the hypotheses, determine the criteria, and perform the evaluations.”

There’s a wonderfully familiar frustration behind that paper. In an informal study of his own working habits, Licklider estimated that about 85% of his thinking time went into preparation: “getting into a position to think.” Finding information, calculating, arranging material so that he could finally make a decision.

He also wanted computers to participate while problems were still taking shape, helping people test possibilities and discover where their reasoning failed. His essay gives me a way to describe what I value in these tools, even across the considerable distance between the computing he described and the systems I’m using.

AI has helped me work through the engineering between wanting those things and being able to play them.

That help leaves me with plenty to do. I choose what belongs in the game. I have to understand what I accept, examine the results, and live with the decisions. An assistant that can produce an answer quickly gives me every reason to become more demanding about the answer.

### Code, and the alleged vital force of typing

I rather liked finding an absolute lack of ceremony in Linus Torvalds’s AudioNoise repository. He describes its Python visualizer as “basically written by vibe-coding,” then explains that he knew little Python and used Google Antigravity to get it done. It’s a small tool for his guitar-pedal experiments. He wanted it to exist, so he used what helped.

His July 2026 remarks put it plainly: “AI is a tool, just like other tools we use. And it’s clearly a useful one.” He also wanted those tools to spare maintainers work, rather than bury them in more of it. I’m happy with both parts of that bargain.

Three is a magical number, so I’ll include a third Linus’ quote, “Linux is not one of those anti-AI projects, and if somebody has issues with that, they can do the open-source thing and fork it. Or just walk away.”

Notch took a more entertaining route. Minecraft’s creator posted “Reject AI” in July, tried AI-assisted coding, and by August was writing, “Maybe it’s time to eat some crow.” Somewhere along the way came the question: “Can I still make fun of vibe coders if I become one?”

The objection I struggle with is that otherwise equivalent code is somehow worse for having been generated. Equally correct, equally readable, equally maintainable: what, exactly, is missing?

This starts to remind me of chemical vitalism. There was a belief that organic substances required a special “vital force” associated with living things, something a chemist couldn’t supply in a laboratory.

Then, in 1828, Friedrich Wöhler synthesized urea and wrote to Berzelius: “I can make urea without needing a kidney, whether of man or dog.”

I find it difficult to believe there’s a comparable necessity for my fingers to have personally visited every key. That said, I’m comfortable using these tools because they’ve helped me make something I care about. I’m equally comfortable putting my name on the result, including the parts I’ll wish I’d checked more thoroughly.

You’re playing my mod. If it eats your saved game, you’re entitled to be annoyed with me.

“Linus does it too” would be a fairly rotten reply to your bug report.

### The artwork is provisional

The artwork though is a more personal matter.

Don’t get me wrong, I care about craftsmanship in programming too. Donald Knuth wrote that programming “produces objects of beauty,” and I agree with him. There’s considerable pleasure in a solution whose parts fit together so well that you can hardly imagine arranging them otherwise.

My difficulty is that an engineering education has done surprisingly little for my ability to draw beautiful things. I can tinker with the artistic tools, adapting one thing, removing another, but I’m no artist myself.

So I used AI art to give these additions a visible form while I built the game. Some of the results I like very much, but I’m still happy to see them replaced.

Heroes III’s artwork is part of why I love it. I want the new images to feel as though they belong beside the old ones, with a real artist’s judgment carried across the whole set.

Please treat all AI-generated artwork in this mod as provisional.

The moving of Tower (now Academy) to a vibrant desert theme, a homage to Heroes V, and, to a lesser extent, Heroes II, took me a lot of time using GIMP, and at times MS-Paint. The result is, in my humble opinion, satisfactory. AI helped immensely along the way, but I’d honestly appreciate a real artist providing his/her vision, and hand.

I’m also satisfied with the missing mage guild levels I supplied this game with, especially Fortress’ new 4th and 5th levels. They are charming, I think, but are a humble shadow of the original game art.

William Morris gave a rule I’m fond of: “Have nothing in your houses which you do not know to be useful or believe to be beautiful.” I’d like the art as a whole in this game to meet a similarly demanding standard.

Human artists are very welcome here. Better human-made artwork that fits Heroes III would be a contribution I’d be genuinely glad to receive (and believe me, there’s plenty of stuff that deserves polishing).

We should agree on the practical details and permission to use the work, and contributors should receive proper credit. Participation is entirely voluntary. Nobody owes this project their labor, and my affection for Heroes III is a poor substitute for someone else’s rent.

I’m offering a place for your work in a game we might both care about.

## The ideas, and the person answerable for them

As for the ideas behind the mod: those are 100% human. I started having them long before I began using these tools.

They grew out of playing Heroes, returning to it, and accumulating increasingly specific opinions about what I wished I could change. Anyone who has loved a game for long enough probably knows how affection can develop an alarming number of footnotes.

My influences include the other Heroes games and the work of people who have kept this one alive. I owe those influences an honest acknowledgment. The particular game I’ve chosen to make, and the decisions about what belongs in it, are my responsibility.

AI helped me give those decisions a working form. It also helped quite a bit polishing this foreword and coming up with punchy lines (even though it’s mostly my own pen and the references are 100% of my own). A disclosure with an undisclosed writing assistant would have been a little too clever.

You may disagree with how I’ve used these tools. You may enjoy the game and dislike some of its pictures. You may have a better idea for something I’ve spent far too long arguing with myself about.

There’s room for that, and I hope there’s room for this project to improve.

I could provide a twenty-page changelog, but its reconstruction is left as an exercise for the player.

I’ve wanted to share this game for a long time.

I hope you have as much fun playing this as I had creating it.

---

## License and source

New Horizons is developed by gandalf196 on the [VCMI engine](https://github.com/vcmi/vcmi). Engine licensing is retained in [license.txt](license.txt), with contributor credits in [AUTHORS.h](AUTHORS.h). Upstream technical documentation remains in [docs/Readme.md](docs/Readme.md).

Selected artwork delivery and provenance are recorded in [NHART_DELIVERY.md](docs/NHART_DELIVERY.md). Original Heroes III Complete game assets are required separately and are not included. A public release is pending.
